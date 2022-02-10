#include "Project.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "ProjectSerializer.h"
#include "sameboy/SameBoySystem.h"
#include "util/SolUtil.h"

using namespace rp;

Project::Project() {
	clear();
}

Project::~Project() {
	if (_lua) {
		delete _lua;
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
		SystemPtr system = _orchestrator->createAudioSystem(entt::type_id<SameBoySystem>().seq(), settings.romPath, settings.sramPath);
		systemSettings[system->getId()] = settings;
	}

	_state.systemSettings = std::move(systemSettings);
}

bool Project::save(std::string_view path) {
	return ProjectSerializer::serialize(path, _state, true);
}

SystemPtr Project::addSystem(SystemType type, std::string_view romPath, std::string_view sramPath) {
	SystemPtr system = _orchestrator->createAudioSystem(type, romPath, sramPath);
	if (system) {
		_state.systemSettings[system->getId()] = SystemSettings{
			.romPath = std::string(romPath),
			.sramPath = std::string(sramPath)
		};
	}

	_version++;

	return system;
}

SystemPtr Project::addSystem(SystemType type, LoadConfig&& loadConfig) {
	SystemPtr system = _orchestrator->createAudioSystem(type, std::forward<LoadConfig>(loadConfig));
	if (system) {
		_state.systemSettings[system->getId()] = SystemSettings();
	}

	_version++;

	return system;
}

void Project::removeSystem(SystemId systemId) {
	_orchestrator->removeSystem(systemId);
	_state.systemSettings.erase(systemId);

	_version++;
}

void Project::duplicateSystem(SystemId systemId) {
	SystemPtr system = _orchestrator->getProcessor().findSystem(systemId);

	LoadConfig loadConfig = {
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.stateBuffer = std::make_shared<Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

	SystemPtr newSystem = _orchestrator->createAudioSystem(system->getType(), std::move(loadConfig));

	SystemSettings newSettings = _state.systemSettings[systemId];
	newSettings.serialized.clear();
	newSettings.models.clear();

	_state.systemSettings[newSystem->getId()] = std::move(newSettings);

	_version++;
}

void Project::clear() {
	_state = ProjectState();

	if (_orchestrator) {
		_orchestrator->clear();
	}

	if (_lua) {
		delete _lua;
	}

	_lua = new sol::state();
	SolUtil::prepareState(*_lua);

	_version++;
}
