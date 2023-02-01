#include "RetroPlugProcessor.h"

#include "core/ProjectSerializer.h"
#include "core/SystemService.h"
#include "foundation/FsUtil.h"

using namespace rp;
using namespace entt::literals;

RetroPlugProcessor::RetroPlugProcessor(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus)
	: _ioMessageBus(messageBus), _typeRegistry(typeRegistry), _systemFactory(systemFactory), _systemManager(systemFactory, messageBus.allocator)
{
	fw::EventNode& node = getEventNode();

	node.receive<SystemIoPtr>([&](SystemIoPtr&& stream) {
		_systemManager.acquireIo(std::move(stream));
	});

	node.receive<FetchStateRequest>([&]() {
		std::vector<SystemStateResponse> systemStates;

		for (size_t i = 0; i < _systemManager.getSystems().size(); ++i) {
			SystemPtr system = _systemManager.getSystems().at(i);

			fw::Uint8Buffer rom = system->getMemory(MemoryType::Rom, AccessType::Read).getBuffer().clone();
			fw::Uint8Buffer state;

			system->saveState(state);

			systemStates.push_back(SystemStateResponse{
				.type = system->getType(),
				.id = system->getId(),
				.romName = system->getRomName(),
				.desc = system->getDesc(),
				.stateOffsets = system->getStateOffsets(),
				.state = std::move(state),
				.rom = std::move(rom),
				.resolution = system->getResolution()
			});
		}

		node.send("Ui"_hs, FetchStateResponse {
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
		node.send("Ui"_hs, CollectSystemEvent{ .system = std::move(system) });
	});

	node.receive<ReplaceSystemEvent>([&](ReplaceSystemEvent&& ev) {
		SystemPtr old = _systemManager.removeSystem(ev.system->getId());
		_systemManager.addSystem(std::move(ev.system));

		node.send("Ui"_hs, CollectSystemEvent{ .system = old });
	});

	node.receive<ResetSystemEvent>([&](ResetSystemEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);
		if (system) {
			system->reset();
		}
	});

	node.receive<SetGameLinkEvent>([&](SetGameLinkEvent&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);

		if (system) {
			if (ev.enabled != system->getGameLink()) {
				if (ev.enabled) {
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
		}
	});

	node.receive<FetchSaveStateRequest>([&](FetchSaveStateRequest&& ev) {
		SystemPtr system = _systemManager.findSystem(ev.systemId);

		if (system) {
			FetchSaveStateResponse res{ .systemId = ev.systemId };
			system->saveState(res.state);
			node.send("Ui"_hs, std::move(res));
		}
	});

	node.receive<PingEvent>([&](PingEvent&& ev) {
		node.send("Ui"_hs, PongEvent{ .time = ev.time });
	});

	node.receive<RemoveAllSystemsEvent>([&]() {
		_systemManager.removeAllSystems();
	});
}

void RetroPlugProcessor::onRender(f32* output, const f32* input, uint32 frameCount) {
	fw::EventNode& ev = getEventNode();
	ev.update();

	size_t sampleCount = (size_t)frameCount * 2;
	fw::Float32Buffer buffer(output, sampleCount);
	buffer.clear();
	
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

	const std::vector<SystemPtr>& systems = _systemManager.getSystems();
	uint32 channel = message.getChannel();

	for (SystemPtr& system : _systemManager.getSystems()) {
		SystemIoPtr io = system->getIo();

		if (!io) {
			io = _ioMessageBus.alloc(system->getId());
			system->setIo(io);
		}
	}

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

	std::string data = ProjectSerializer::serialize(_typeRegistry, _projectState, systemDescs);
	target.resize(target.size() + data.size());
	target.write((uint8*)data.data(), data.size());
}

void RetroPlugProcessor::onDeserialize(const fw::Uint8Buffer& source) {
	ProjectState projectState;
	std::vector<SystemDesc> systemDescs;
	std::string_view fileData((const char*)source.data(), source.size());

	if (ProjectSerializer::deserializeFromMemory(_typeRegistry, fileData, projectState, systemDescs)) {
		_projectState = std::move(projectState);
		std::vector<SystemDesc> systemDescs = std::move(systemDescs);

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
				system->setSampleRate((uint32)getSampleRate());
				system->load(std::move(loadConfig));

				_systemManager.addSystem(std::move(system));
			} else {
				spdlog::error("Failed to find a system type that can load rom {}", desc.paths.romPath);
			}
		}
	}
}
