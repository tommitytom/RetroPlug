#include "Project.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

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

void Project::deserializeModel(SystemId systemId, SystemWrapperPtr system, std::shared_ptr<Model> model) {
	const std::string& serialized = system->getSettings().serialized;

	if (serialized.size()) {
		sol::table modelTable;

		if (SolUtil::deserializeTable(*_lua, serialized, modelTable)) {
			sol::table m = modelTable[model->getName()];

			if (m.valid()) {
				spdlog::info("Deserializing data for {}", model->getName());
				model->onDeserialize(*_lua, m);
			}
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
			// LOG
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

void Project::duplicateSystem(SystemId systemId, const SystemSettings& settings) {
	SystemWrapperPtr systemWrapper = findSystem(systemId);
	SystemPtr system = systemWrapper->getSystem();

	LoadConfig loadConfig = {
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.stateBuffer = std::make_shared<Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

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
	_state = ProjectState();

	if (_lua) {
		delete _lua;
	}

	_lua = new sol::state();
	SolUtil::prepareState(*_lua);

	_version++;
	_requiresSave = false;
}
