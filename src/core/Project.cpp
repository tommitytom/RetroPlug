#include "Project.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "platform/AudioManager.h"
#include "core/ProjectSerializer.h"
#include "sameboy/SameBoySystem.h"
#include "util/SolUtil.h"
#include "util/fs.h"

using namespace rp;

Project::Project() {
	clear();
}

Project::~Project() {
	if (_lua) {
		delete _lua;
	}
}

void Project::processIncoming() {
	OrchestratorChange change;
	while (_messageBus->audioToUi.try_dequeue(change)) {
		if (change.swap) {
			// TODO: Remove placeholder?
			_processor->addSystem(change.swap);
		}
	}
}

std::string rp::Project::getName() {
	std::string name;
	std::unordered_map<std::string, size_t> romNames;

	for (SystemWrapperPtr& systemWrapper : _systems) {
		SystemPtr system = systemWrapper->getSystem();

		auto found = romNames.find(system->getRomName());

		if (found != romNames.end()) {
			found->second++;
		} else {
			romNames[system->getRomName()] = 1;
		}
	}

	bool first = true;
	for (auto v : romNames) {
		if (!first) {
			name += " | ";
		}

		if (v.second == 1) {
			name += v.first;
		} else {
			name += fmt::format("{}x {}", v.second, v.first);
		}
	}

	for (SystemWrapperPtr& system : _systems) {
		for (auto [type, model] : system->getModels()) {
			std::string modelName = model->getProjectName();

			if (modelName.size() > 0) {
				return fmt::format("{} [{}]", modelName, name);
			}
		}
	}

	return name;
}

void Project::update(f32 delta) {
	f32 sr = (f32)_audioManager->getSampleRate();
	uint32 frameCount = (uint32)(sr * delta);

	std::vector<uint64> offsets;
	
	for (SystemWrapperPtr system : _systems) {
		system->update(delta);

		SystemIoPtr& io = system->getSystem()->getStream();
		if (io) {
			io->output.audio = std::make_shared<Float32Buffer>(frameCount * 2);
			offsets.push_back(system->getSystem()->getProcessedAudioFrames());
		}
	}
	
	_processor->process(frameCount);

	// Move outputs to inputs!
	for (size_t i = 0; i < _systems.size(); ++i) {
		SystemIoPtr& io = _systems[i]->getSystem()->getStream();

		if (io) {
			uint64 nextOffset = offsets[i] + io->output.audio->size() / 2;
			if (nextOffset != _systems[i]->getSystem()->getProcessedAudioFrames()) {
				spdlog::error("MISMATCH");
			}

			io->input.audioChunks.push_back(AudioChunk{
				.buffer = std::move(io->output.audio),
				.offset = offsets[i]
			});
		}
	}
}

void Project::load(std::string_view path) {
	clear();

	std::vector<SystemSettings> systemSettings;
	
	if (!ProjectSerializer::deserialize(path, _state, systemSettings)) {
		spdlog::error("Failed to load project at {}", path);
		return;
	}

	// Create systems from new state
	for (const SystemSettings& settings : systemSettings) {
		addSystem<SameBoySystem>(settings);
	}

	_requiresSave = false;
}

bool Project::save(std::string_view path) {
	std::vector<SystemSettings> settings;
	for (SystemWrapperPtr system : _systems) {
		settings.push_back(system->getSettings());
	}

	return ProjectSerializer::serialize(path, _state, _systems, true);
}

SystemWrapperPtr Project::addSystem(SystemType type, const SystemSettings& settings, SystemId systemId) {
	LoadConfig loadConfig = LoadConfig{
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.sramBuffer = std::make_shared<Uint8Buffer>()
	};

	if (!fsutil::readFile(settings.romPath, loadConfig.romBuffer.get())) {
		return nullptr;
	}

	if (settings.sramPath.size()) {
		loadConfig.sramBuffer = std::make_shared<Uint8Buffer>();
		if (!fsutil::readFile(settings.sramPath, loadConfig.sramBuffer.get())) {
			spdlog::error("Failed to read SRAM from {}", settings.sramPath);
		}
	}

	return addSystem(type, settings, std::move(loadConfig), systemId);
}

SystemWrapperPtr Project::addSystem(SystemType type, const SystemSettings& settings, LoadConfig&& loadConfig, SystemId systemId) {
	if (systemId == INVALID_SYSTEM_ID) {
		systemId = _nextId++;
	}

	SystemWrapperPtr system = std::make_shared<SystemWrapper>(systemId, _processor, _messageBus, &_modelFactory);
	system->load(settings, std::forward<LoadConfig>(loadConfig));

	_systems.push_back(system);
	_version++;

	if (_state.settings.autoSave) {
		_requiresSave = true;
	}

	return system;
}

void Project::removeSystem(SystemId systemId) {
	for (size_t i = 0; i < _systems.size(); ++i) {
		SystemWrapperPtr system = _systems[i];

		if (system->getId() == systemId) {
			_systems.erase(_systems.begin() + i);
		}
	}

	_messageBus->uiToAudio.enqueue(OrchestratorChange{ .remove = systemId });

	_version++;

	if (_state.settings.autoSave) {
		_requiresSave = true;
	}
}

void Project::duplicateSystem(SystemId systemId, SystemSettings settings) {
	SystemWrapperPtr systemWrapper = findSystem(systemId);
	SystemPtr system = systemWrapper->getSystem();

	LoadConfig loadConfig = {
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.stateBuffer = std::make_shared<Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

	settings.serialized = ProjectSerializer::serializeModels(systemWrapper);

	auto cloned = addSystem(system->getType(), settings, std::move(loadConfig));
	cloned->saveSram();
}

SystemWrapperPtr Project::findSystem(SystemId systemId) {
	for (size_t i = 0; i < _systems.size(); ++i) {
		SystemWrapperPtr system = _systems[i];

		if (system->getId() == systemId) {
			return system;
		}
	}

	return nullptr;
}

void Project::clear() {
	// Remove systems
	std::vector<SystemId> systemIds;
	for (auto system : _systems) {
		systemIds.push_back(system->getId());
	}

	for (SystemId systemId : systemIds) {
		removeSystem(systemId);
	}

	_state = ProjectState();

	if (_lua) {
		delete _lua;
	}

	_lua = new sol::state();
	SolUtil::prepareState(*_lua);

	_version++;
	_requiresSave = false;
	//_copyLocal = true;
}
