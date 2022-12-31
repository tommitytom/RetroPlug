#include "Project.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"

#include "core/LuaUtil.h"
#include "core/ProjectSerializer.h"

#include "sameboy/SameBoySystem.h"

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

	for (SystemWrapperPtr systemWrapper : _systems) {
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

	for (SystemWrapperPtr system : _systems) {
		for (auto [type, model] : system->getModels()) {
			std::string modelName = model->getProjectName();

			if (modelName.size() > 0) {
				return fmt::format("{} [{}]", modelName, name);
			}
		}
	}

	return name;
}

void Project::load(std::string_view path) {
	clear();

	std::vector<SystemDesc> systemDescs;
	
	if (!ProjectSerializer::deserialize(path, _state, systemDescs)) {
		spdlog::error("Failed to load project at {}", path);
		return;
	}

	// Create systems from new state
	for (const SystemDesc& desc : systemDescs) {
		addSystem<SameBoySystem>(desc);
	}

	_requiresSave = false;
}

bool Project::save(std::string_view path) {
	return ProjectSerializer::serialize(path, _state, _systems, true);
}

SystemWrapperPtr Project::addSystem(SystemType type, const SystemDesc& systemDesc, SystemId systemId) {
	LoadConfig loadConfig = LoadConfig{
		.romBuffer = std::make_shared<fw::Uint8Buffer>(),
		.sramBuffer = std::make_shared<fw::Uint8Buffer>()
	};

	if (!fw::FsUtil::readFile(systemDesc.paths.romPath, loadConfig.romBuffer.get())) {
		return nullptr;
	}

	if (systemDesc.paths.sramPath.size()) {
		loadConfig.sramBuffer = std::make_shared<fw::Uint8Buffer>();
		if (!fw::FsUtil::readFile(systemDesc.paths.sramPath, loadConfig.sramBuffer.get())) {
			// LOG
		}
	}

	return addSystem(type, systemDesc, std::move(loadConfig), systemId);
}

SystemWrapperPtr Project::addSystem(SystemType type, const SystemDesc& systemDesc, LoadConfig&& loadConfig, SystemId systemId) {
	if (systemId == INVALID_SYSTEM_ID) {
		systemId = _nextId++;
	}

	SystemWrapperPtr system = std::make_shared<SystemWrapper>(systemId, _processor, _messageBus, &_modelFactory);
	system->load(systemDesc, std::forward<LoadConfig>(loadConfig));

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

void Project::duplicateSystem(SystemId systemId, SystemDesc desc) {
	SystemWrapperPtr systemWrapper = findSystem(systemId);
	SystemPtr system = systemWrapper->getSystem();

	LoadConfig loadConfig = {
		.romBuffer = std::make_shared<fw::Uint8Buffer>(),
		.stateBuffer = std::make_shared<fw::Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

	desc.settings.serialized = ProjectSerializer::serializeModels(systemWrapper);

	auto cloned = addSystem(system->getType(), desc, std::move(loadConfig));
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
	rp::LuaUtil::prepareState(*_lua);

	_version++;
	_requiresSave = false;
	//_copyLocal = true;
}
