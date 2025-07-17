#include "Project.h"

#include <spdlog/spdlog.h>
#include <entt/core/hashed_string.hpp>

#include "foundation/FsUtil.h"

#include "core/LuaUtil.h"
#include "core/ProjectSerializer.h"

#include "sameboy/SameBoySystem.h"
#include "core/ProxySystem.h"
#include "core/ProxySystemService.h"
#include "core/SystemService.h"
#include "core/SystemServiceProvider.h"

using namespace entt::literals;

using namespace rp;

Project::Project(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, ConcurrentPoolAllocator<SystemIo>& ioAllocator)
	: _typeRegistry(typeRegistry)
	, _systemFactory(systemFactory)
	, _systemManager(systemFactory, ioAllocator)
	, _ioAllocator(ioAllocator)
{
	clear();
}

Project::~Project() {

}

void Project::setup(fw::EventNode& eventNode, FetchStateResponse&& state) {
	assert(!_eventNode);

	_eventNode = &eventNode;
	_config = std::move(state.config);
	_state = std::move(state.project);

	for (SystemStateResponse& systemState : state.systems) {
		if (_nextId <= systemState.id) {
			_nextId = systemState.id + 1;
		}

		std::shared_ptr<ProxySystem> system = std::make_shared<ProxySystem>(
			systemState.type,
			systemState.id,
			systemState.romName,
			std::move(systemState.rom),
			std::move(systemState.state),
			eventNode,
			systemState.stateOffsets
		);

		system->setDesc(std::move(systemState.desc));
		system->setResolution(systemState.resolution);

		for (const auto& [type, state] : systemState.services) {
			spdlog::critical("adding service type: {}", type);
			system->addService(std::make_shared<ProxySystemService>(type, state));
		}

		_systemManager.addSystem(system);
	}

	_version++;
	_requiresSave = false;
}

std::string rp::Project::getName() {
	std::string name;

	if (!_state.path.empty()) {
		std::filesystem::path path = _state.path;
		name = path.filename().string();
	}

	std::unordered_map<std::string, size_t> romNames;


	for (const SystemPtr& system : _systemManager.getSystems()) {
		if (name.size() > 0) {
			name += " - ";
		}

		bool hasName = false;
		for (const auto& service : system->getServices()) {
			const SystemServiceProviderPtr& provider = _systemFactory.findServiceProvider(service->getType());
			std::string modelName = provider->getProjectName(*system);
			if (modelName.size() > 0) {
				name += modelName;
				hasName = true;
				break;
			}
		}

		if (!hasName) {
			name += system->getRomName();
			hasName = true;
		}

		/*if (!hasName) {
			auto found = romNames.find(system->getRomName());

			if (found != romNames.end()) {
				found->second++;
			} else {
				romNames[system->getRomName()] = 1;
			}
		}*/
	}

	/*bool first = true;
	for (auto v : romNames) {
		if (!first) {
			name += " | ";
		}

		if (v.second == 1) {
			name += v.first;
		} else {
			name += fmt::format("{}x {}", v.second, v.first);
		}

		first = false;
	}*/

	return name;
}

bool Project::load(std::string_view path) {
	ProjectState projectState;
	std::vector<SystemDesc> systemDescs;

	if (!ProjectSerializer::deserializeFromFile(_typeRegistry, path, projectState, systemDescs)) {
		spdlog::error("Failed to load project at {}", path);
		return false;
	}

	clear();

	_state = std::move(projectState);

	// Create systems from new state
	for (const SystemDesc& desc : systemDescs) {
		std::vector<SystemType> systemTypes = _systemFactory.getRomLoaders(desc.paths.romPath);

		if (systemTypes.size() > 0) {
			addSystem(systemTypes[0], desc);
		} else {
			spdlog::error("Failed to find a system that can load rom {}", desc.paths.romPath);
		}
	}

	_requiresSave = false;
	return true;
}

bool Project::save() {
	std::vector<SystemDesc> systemDescs;

	for (SystemPtr& system : _systemManager.getSystems()) {
		systemDescs.push_back(system->getDesc());
	}

	if (ProjectSerializer::serialize(_typeRegistry, _state.path, _state, systemDescs, false)) {
		_requiresSave = false;
		return true;
	}



	return false;
}

SystemPtr Project::addSystem(SystemType type, const SystemDesc& systemDesc, SystemId systemId) {
	LoadConfig loadConfig = LoadConfig{
		.desc = systemDesc,
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

	return addSystem(type, std::move(loadConfig), systemId);
}

SystemPtr Project::addSystem(SystemType type, LoadConfig&& loadConfig, SystemId systemId) {
	if (systemId == INVALID_SYSTEM_ID) {
		systemId = _nextId++;
	}

	SystemPtr system = _systemFactory.createSystem(systemId, type);

	std::vector<SystemServiceType> serviceTypes = _systemFactory.getRelevantServiceTypes(loadConfig);
	for (SystemServiceType type : serviceTypes) {
		SystemServicePtr service = _systemFactory.createSystemService(type);

		auto found = loadConfig.desc.services.find(service->getType());
		if (found != loadConfig.desc.services.end()) {
			service->setState(found->second);
			found->second = service->getState();
			//assert(!found->second.owner());
		}

		service->onBeforeLoad(loadConfig);

		system->addService(service);
	}

	system->load(std::forward<LoadConfig>(loadConfig));

	for (SystemServicePtr& service : system->getServices()) {
		service->onAfterLoad(*system);
	}

	fw::Uint8Buffer romData = system->getMemory(MemoryType::Rom, AccessType::Read).getBuffer().clone();
	fw::Uint8Buffer stateData;
	system->saveState(stateData);

	std::shared_ptr<ProxySystem> proxySystem = std::make_shared<ProxySystem>(
		type,
		systemId,
		system->getRomName(),
		std::move(romData),
		std::move(stateData),
		*_eventNode,
		system->getStateOffsets()
	);

	proxySystem->setResolution(system->getResolution());

	SystemDesc proxyDesc = system->getDesc();
	for (SystemServicePtr& service : system->getServices()) {
		ProxySystemServicePtr proxyService = std::make_shared<ProxySystemService>(service->getType(), service->getState());
		proxySystem->addService(proxyService);
		proxyDesc.services[proxyService->getType()] = proxyService->getState();
	}

	proxySystem->setDesc(std::move(proxyDesc));

	_systemManager.addSystem(proxySystem);

	_eventNode->send("Audio"_hs, AddSystemEvent{ .system = std::move(system) });

	_version++;
	_requiresSave = true;

	return proxySystem;
}

void Project::removeSystem(SystemId systemId) {
	_systemManager.removeSystem(systemId);
	_eventNode->send("Audio"_hs, RemoveSystemEvent{ .systemId = systemId });

	_version++;
	_requiresSave = true;
}

SystemPtr Project::duplicateSystem(SystemId systemId) {
	SystemPtr system = _systemManager.findSystem(systemId);

	LoadConfig loadConfig = {
		.desc = system->getDesc(),
		.romBuffer = std::make_shared<fw::Uint8Buffer>(),
		.stateBuffer = std::make_shared<fw::Uint8Buffer>()
	};

	system->saveState(*loadConfig.stateBuffer);

	MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
	romData.getBuffer().copyTo(loadConfig.romBuffer.get());

	//desc.settings.serialized = ProjectSerializer::serializeModels(systemWrapper);
	//cloned->saveSram();

	return addSystem(system->getTargetType(), std::move(loadConfig));
}

void Project::clear() {
	_systemManager.removeAllSystems();

	if (_eventNode) {
		_eventNode->send("Audio"_hs, RemoveAllSystemsEvent{});
	}

	_state = ProjectState();

	_version++;
	_requiresSave = false;
	//_copyLocal = true;
}
