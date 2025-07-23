#include "RetroPlugProcessor.h"

#include "core/ProjectExporter.h"
#include "core/ProjectSerializer.h"
#include "core/SystemService.h"
#include "foundation/FsUtil.h"

using namespace rp;
using namespace entt::literals;

SystemServicePtr findService(SystemPtr system, SystemServiceType type) {
	for (SystemServicePtr service : system->getServices()) {
		if (service->getType() == type) {
			return service;
		}
	}

	return nullptr;
}

RetroPlugProcessor::RetroPlugProcessor(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus, const RetroPlugConfig& config)
	: _ioMessageBus(messageBus), _typeRegistry(typeRegistry), _systemFactory(systemFactory), _systemManager(systemFactory, messageBus.allocator)
{
	fw::EventNode& node = getEventNode();

	node.receive<SystemIoPtr>([&](SystemIoPtr&& stream) {
		_systemManager.acquireIo(std::move(stream));
	});

	node.receive<SetProjectState>([&](SetProjectState&& ev) {
		_projectState = std::move(ev.project);
	});

	node.receive<FetchStateRequest>([&]() {
		std::vector<SystemStateResponse> systemStates;

		for (size_t i = 0; i < _systemManager.getSystems().size(); ++i) {
			SystemPtr system = _systemManager.getSystems().at(i);

			fw::Uint8Buffer rom = system->getMemory(MemoryType::Rom, AccessType::Read).getBuffer().clone();
			fw::Uint8Buffer state;

			system->saveState(state);

			std::vector<std::pair<SystemServiceType, entt::any>> services;
			for (const auto& service : system->getServices()) {
				services.push_back({ service->getType(), service->getState() });
			}

			systemStates.push_back(SystemStateResponse{
				.type = system->getType(),
				.id = system->getId(),
				.romName = system->getRomName(),
				.desc = system->getDesc(),
				.stateOffsets = system->getStateOffsets(),
				.state = std::move(state),
				.rom = std::move(rom),
				.resolution = system->getResolution(),
				.services = std::move(services)
			});
		}

		node.trySend("Ui"_hs, FetchStateResponse {
			.config = _config,
			.project = _projectState,
			.systems = std::move(systemStates)
		});
	});

	node.receive<AddSystemEvent>([&](AddSystemEvent&& ev) {
		ev.system->setSampleRate((uint32)getSampleRate());
		_systemManager.addSystem(ev.system);
	});

	node.receive<RemoveSystemEvent>([&](const RemoveSystemEvent& ev) {
		SystemPtr system = _systemManager.removeSystem(ev.systemId);
		node.trySend("Ui"_hs, CollectSystemEvent{ .system = std::move(system) });
	});

	node.receive<ReplaceSystemEvent>([&](ReplaceSystemEvent&& ev) {
		SystemPtr old = _systemManager.removeSystem(ev.system->getId());
		_systemManager.addSystem(std::move(ev.system));

		node.trySend("Ui"_hs, CollectSystemEvent{ .system = old });
	});

	node.receive<ResetSystemEvent>([&](ResetSystemEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			system->reset();
		}
	});

	node.receive<LoadRomEvent>([&](LoadRomEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			system->loadRom(std::move(ev.romBuffer));
		}
	});

	node.receive<SetSettingsEvent>([&](SetSettingsEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			SystemDesc desc = system->getDesc();

			if (desc.settings.gameLink != ev.settings.gameLink) {
				if (ev.settings.gameLink) {
					for (SystemPtr other : _systemManager.getSystems()) {
						if (other->getGameLink()) {
							other->addLinkTarget(system.get());
							system->addLinkTarget(other.get());
						}
					}

					system->setGameLink(true);
				} else {
					for (SystemPtr other : _systemManager.getSystems()) {
						if (other->getGameLink()) {
							other->removeLinkTarget(system.get());
						}
					}

					system->setGameLink(false);
				}
			}

			desc.settings = ev.settings;
			system->setDesc(std::move(desc));
		}
	});

	node.receive<FetchSaveStateRequest>([&](FetchSaveStateRequest&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);

		if (system) {
			FetchSaveStateResponse res{ .systemId = ev.systemId };
			system->saveState(res.state);
			node.trySend("Ui"_hs, std::move(res));
		}
	});

	node.receive<PingEvent>([&](PingEvent&& ev) {
		node.trySend("Ui"_hs, PongEvent{ .time = ev.time });
	});

	node.receive<RemoveAllSystemsEvent>([&]() {
		_systemManager.removeAllSystems();
	});

	node.receive<SystemServiceEvent>([&](SystemServiceEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);

		if (system) {
			SystemServicePtr service = findService(system, ev.systemServiceType);

			if (service) {
				service->receiveEvent(ev.data);
			}
		}
	});

	node.receive<SystemServiceDataEvent>([&](SystemServiceDataEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		
		if (system) {
			SystemServicePtr service = findService(system, ev.systemServiceType);
			
			if (service) {
				entt::any state = service->getState();
				assert(!state.owner());
				ev.caller(state, ev.arg);
			}
		}
	});

	node.receive<LoadSramEvent>([&](LoadSramEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			system->loadSram(std::move(ev.sramBuffer));
		}
	});

	node.receive<LoadStateEvent>([&](LoadStateEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			system->loadState(std::move(ev.stateBuffer));
		}
	});
}

void RetroPlugProcessor::onTransportChange(bool playing) {
	for (SystemPtr& system : _systemManager.getSystems()) {
		for (SystemServicePtr& service : system->getServices()) {
			service->onTransportChange(*system, playing);
		}
	}
}

void RetroPlugProcessor::onTransportUpdate(const fw::TimeInfo& timeInfo) {
	for (SystemPtr& system : _systemManager.getSystems()) {
		for (SystemServicePtr& service : system->getServices()) {
			service->onTransportUpdate(*system, timeInfo);
		}
	}
}

void RetroPlugProcessor::onBeginUpdate(uint32 frameCount) {
	size_t sampleCount = (size_t)frameCount * 2;
	fw::EventNode& ev = getEventNode();

	ev.update();

	// Make sure systems have output buffers set
	for (SystemPtr& system : _systemManager.getSystems()) {
		SystemIoPtr io = system->getIo();

		if (!io) {
			io = _ioMessageBus.alloc(system->getId());
			system->setIo(io);
		}

		if (io) {
			io->output.audio = std::make_shared<fw::Float32Buffer>(sampleCount);
		}
	}
}

void RetroPlugProcessor::onRender(f32* output, const f32* input, uint32 frameCount) {
	size_t sampleCount = (size_t)frameCount * 2;
	fw::EventNode& ev = getEventNode();

	fw::Float32Buffer buffer(output, sampleCount);
	buffer.clear();

	_systemManager.process(frameCount);

	// Combine output of systems!

	for (SystemPtr& system : _systemManager.getSystems()) {
		SystemIoPtr io = system->getIo();
		
		if (io->output.audio) {
			for (uint32 i = 0; i < sampleCount; ++i) {
				buffer[i] = buffer[i] + io->output.audio->get(i);
			}
		}

		if (io->output.serial.size()) {
			// TODO: Send midi data out
		}

		io->input.reset();

		ev.trySend("Ui"_hs, system->releaseIo());
	}
}

void RetroPlugProcessor::onMidi(const fw::MidiMessage& message) {
	fw::EventNode& ev = getEventNode();
	ev.update();

	for (SystemPtr& system : _systemManager.getSystems()) {
		SystemIoPtr io = system->getIo();

		if (!io) {
			io = _ioMessageBus.alloc(system->getId());
			system->setIo(io);
		}
	}

	const std::vector<SystemPtr>& systems = _systemManager.getSystems();
	uint32 channel = message.getChannel();

	switch (_projectState.settings.midiRouting) {
		case MidiChannelRouting::SendToAll: {
			for (size_t i = 0; i < systems.size(); i++) {
				for (SystemServicePtr& service : systems[i]->getServices()) {
					service->onMidi(*systems[i], message);
				}
			}

			break;
		}
		case MidiChannelRouting::OneChannelPerInstance: {
			if (channel < systems.size()) {
				fw::MidiMessage msg = message;
				msg.setChannel(0);

				for (SystemServicePtr& service : systems[channel]->getServices()) {	
					service->onMidi(*systems[channel], msg);
				}
			}

			break;
		}
		case MidiChannelRouting::FourChannelsPerInstance: {
			if (channel < systems.size() * 4) {
				uint32 ch = channel % 4;
				fw::MidiMessage msg = message;
				msg.setChannel(ch);

				for (SystemServicePtr& service : systems[ch]->getServices()) {
					service->onMidi(*systems[ch], msg);
				}
			}

			break;
		}
	}
}

void RetroPlugProcessor::onSampleRateChange(f32 sampleRate) {
	for (SystemPtr& system : _systemManager.getSystems()) {
		system->setSampleRate((uint32)sampleRate);
	}
}

void RetroPlugProcessor::onSerialize(fw::Uint8Buffer& target) {
	std::vector<SystemDesc> systemDescs;
	for (SystemPtr system : _systemManager.getSystems()) {
		systemDescs.push_back(system->getDesc());
	}

	ProjectExporter::Settings settings = {
		.project = true,
		.includeFiles = false, // We don't need to export roms and savs here
		.samples = false
	};

	fw::Uint8Buffer data;
	if (!ProjectExporter::exportProject(settings, _typeRegistry, _projectState, _systemManager.getSystems(), data)) {
		spdlog::error("Failed to serialize project data for saving");
		return;
	}

	target.resize(target.size() + data.size());
	target.write((uint8*)data.data(), data.size());
}

void RetroPlugProcessor::onDeserialize(const fw::Uint8Buffer& source) {
	ProjectState projectState;
	std::vector<SystemDesc> systemDescs;
	std::string_view fileData((const char*)source.data(), source.size());

	if (ProjectSerializer::deserializeFromMemory(_typeRegistry, fileData, projectState, systemDescs)) {
		_projectState = std::move(projectState);

		uint32 systemId = 1;

		for (const SystemDesc& desc : systemDescs) {
			std::vector<SystemType> systemTypes = _systemFactory.getRomLoaders(desc.paths.romPath);

			if (systemTypes.size() > 0) {
				LoadConfig loadConfig = LoadConfig{
					.desc = desc,
					.romBuffer = std::make_shared<fw::Uint8Buffer>()
				};

				if (!fw::FsUtil::readFile(desc.paths.romPath, loadConfig.romBuffer.get())) {
					spdlog::error("Failed to create system: Rom does not exist at {}", desc.paths.romPath);
					continue;
				}

				if (desc.paths.sramPath.size()) {
					loadConfig.sramBuffer = std::make_shared<fw::Uint8Buffer>();

					if (!fw::FsUtil::readFile(desc.paths.sramPath, loadConfig.sramBuffer.get())) {
						spdlog::error("Failed to load system SRAM: File does not exist at {}", desc.paths.sramPath);
						continue;
					}
				}

				SystemPtr system = _systemFactory.createSystem(systemId++, systemTypes[0]);
				system->load(std::move(loadConfig));
				system->setSampleRate((uint32)getSampleRate());

				_systemManager.addSystem(std::move(system));
			} else {
				spdlog::error("Failed to find a system type that can load rom {}", desc.paths.romPath);
			}
		}
	}
}
