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

void Project::deserializeModel(SystemId systemId, std::shared_ptr<Model> model) {
	const std::string& serialized = _state.systemSettings[systemId].serialized;

	if (serialized.size()) {
		sol::table modelTable;

		if (SolUtil::deserializeTable(*_lua, serialized, modelTable)) {
			sol::table m = modelTable[model->getName()];

			if (m.valid()) {
				spdlog::info("Deserializing data for {}", model->getName());
				model->deserialize(*_lua, m);
			}
		}
	}
}

void Project::load(std::string_view path) {
	clear();

	if (!ProjectSerializer::deserialize(path, _state)) {
		spdlog::error("Failed to load project at {}", path);
		return;
	}

	std::map<SystemId, SystemSettings> systemSettings;

	// Create systems from new state
	for (auto& [systemId, settings] : _state.systemSettings) {
		SystemWrapperPtr system = addSystem<SameBoySystem>(settings.romPath, settings.sramPath);
		systemSettings[system->getId()] = settings;
	}

	_state.systemSettings = std::move(systemSettings);
}

bool Project::save(std::string_view path) {
	return ProjectSerializer::serialize(path, _state, true);
}

SystemWrapperPtr Project::addSystem(SystemType type, std::string_view romPath, std::string_view sramPath, SystemId systemId) {
	LoadConfig loadConfig = LoadConfig{
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.sramBuffer = std::make_shared<Uint8Buffer>()
	};

	if (!fsutil::readFile(romPath, loadConfig.romBuffer.get())) {
		return nullptr;
	}

	if (sramPath.size()) {
		loadConfig.sramBuffer = std::make_shared<Uint8Buffer>();
		if (!fsutil::readFile(sramPath, loadConfig.sramBuffer.get())) {
			// LOG
		}
	}

	return addSystem(type, std::move(loadConfig), systemId);
}

SystemWrapperPtr Project::addSystem(SystemType type, LoadConfig&& loadConfig, SystemId systemId) {
	if (systemId == INVALID_SYSTEM_ID) {
		systemId = _nextId++;
	}

	SystemWrapperPtr system = std::make_shared<SystemWrapper>(systemId, _processor, _messageBus, &_modelFactory);
	system->load(std::forward<LoadConfig>(loadConfig));

	_state.systemSettings[systemId] = SystemSettings();
	_systems.push_back(system);
	_version++;

	return system;
}

void Project::removeSystem(SystemId systemId) {
	for (size_t i = 0; i < _systems.size(); ++i) {
		SystemWrapperPtr system = _systems[i];

		if (system->getId() == systemId) {
			_systems.erase(_systems.begin() + i);
		}
	}

	_state.systemSettings.erase(systemId);
	_messageBus->uiToAudio.enqueue(OrchestratorChange{ .remove = systemId });

	_version++;
}

void Project::duplicateSystem(SystemId systemId) {
	SystemWrapperPtr systemWrapper = findSystem(systemId);
	SystemPtr system = systemWrapper->getSystem();

	LoadConfig loadConfig = {
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.stateBuffer = std::make_shared<Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

	SystemWrapperPtr newSystem = addSystem(system->getType(), std::move(loadConfig));

	SystemSettings newSettings = _state.systemSettings[systemId];
	newSettings.serialized.clear();

	_state.systemSettings[newSystem->getId()] = std::move(newSettings);
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
}
