#include "Project.h"

#include <spdlog/spdlog.h>
#include <entt/core/hashed_string.hpp>

#include "foundation/FsUtil.h"

#include "core/LuaUtil.h"
#include "core/ProjectSerializer.h"
#include "core/ProxySystem.h"
#include "core/ProxySystemService.h"
#include "core/SystemService.h"
#include "core/SystemServiceProvider.h"

#include "sameboy/SameBoySystem.h"

using namespace entt::literals;

namespace rp {
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
				std::string romFilename = fw::FsUtil::getFilename(system->getDesc().paths.romPath);
				name += system->getRomName();
				
				if (romFilename.size()) {
					name += " (" + romFilename + ")";
				}
				
				hasName = true;
			}
		}

		if (!name.empty()) {
			return name;
		}

		if (!_state.path.empty()) {
			return fw::FsUtil::getFilename(_state.path);
		}

		return "Unknown";
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

	fs::path getUniqueFilename(const fs::path& suggested) {
		if (!fs::exists(suggested)) {
			return suggested;
		}

		size_t countStart = 2;
		size_t countMax = 99999;
		fs::path fullTargetPath;
		fs::path fullTargetDir = suggested.parent_path();
		std::string filename = suggested.filename().string();

		size_t dashFound = filename.find_last_of('-');
		size_t dotFound = filename.find_last_of('.');
		assert(dotFound != std::string::npos);

		std::string filenameWithoutExt = filename.substr(0, dotFound);
		std::string extension = filename.substr(dotFound);

		if (dashFound < dotFound) {
			filenameWithoutExt = filenameWithoutExt.substr(0, dashFound);

			std::string afterDash = filename.substr(dashFound, dotFound - dashFound);
			try {
				countStart = std::stoi(afterDash);
				countStart++;

				filename = filename.substr(dashFound + 1);
			} catch (...) {
				// Text before dash is not a number, ignore it
			}
		}

		for (size_t i = countStart; i < countMax; ++i) {
			fullTargetPath = fullTargetDir / fmt::format("{}-{}{}", filenameWithoutExt, i, extension);

			if (!fs::exists(fullTargetPath)) {
				return fullTargetPath;
			}
		}

		spdlog::error("Failed to create unique filename!");
		return "";
	}

	bool Project::save() {
		std::vector<SystemDesc> systemDescs;

		for (const SystemPtr& system : _systemManager.getSystems()) {
			SystemDesc desc = system->getDesc();

			fw::Uint8Buffer buffer;

			if (_state.settings.saveType == SaveStateType::Sram) {
				if (desc.paths.sramPath.empty()) {
					desc.paths.sramPath = getUniqueFilename(fw::FsUtil::replaceFileExt(desc.paths.romPath, ".sav")).string();
				}
					
				spdlog::info("Saving SRAM for system {} to {}", system->getId(), desc.paths.sramPath);

				system->saveSram(buffer);
				if (!fw::FsUtil::writeFile(desc.paths.sramPath, buffer)) {
					spdlog::error("Failed to write SRAM for system {}", system->getId());
				}
			} else if (_state.settings.saveType == SaveStateType::State) {
				if (desc.paths.statePath.empty()) {
					desc.paths.statePath = getUniqueFilename(fw::FsUtil::replaceFileExt(desc.paths.romPath, ".state")).string();
				}

				spdlog::info("Saving state for system {} to {}", system->getId(), desc.paths.statePath);

				system->saveState(buffer);
				if (!fw::FsUtil::writeFile(desc.paths.statePath, buffer)) {
					spdlog::error("Failed to write SRAM for system {}", system->getId());
				}
			}

			system->setDesc(desc);
			systemDescs.push_back(desc);
		}

		if (_state.path.empty() && systemDescs.size()) {
			const SystemDesc& desc = systemDescs[0];

			if (_state.settings.saveType == SaveStateType::State && desc.paths.statePath.size()) {
				_state.path = fw::FsUtil::replaceFileExt(desc.paths.statePath, ".rplg", false);
			} else if (_state.settings.saveType == SaveStateType::Sram && desc.paths.sramPath.size()) {
				_state.path = fw::FsUtil::replaceFileExt(desc.paths.sramPath, ".rplg", false);
			} else {
				spdlog::error("Unknown save type: {}", _state.settings.saveType);
				return false;
			}
		}

		if (_state.path.empty()) {
			return false;
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
			spdlog::error("Failed to read ROM file at {}", systemDesc.paths.romPath);
			return nullptr;
		}

		if (systemDesc.paths.sramPath.size()) {
			loadConfig.sramBuffer = std::make_shared<fw::Uint8Buffer>();
			if (!fw::FsUtil::readFile(systemDesc.paths.sramPath, loadConfig.sramBuffer.get())) {
				spdlog::warn("Failed to read SRAM file at {}", systemDesc.paths.sramPath);
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

		loadConfig.desc.paths.sramPath = "";
		loadConfig.desc.paths.statePath = "";

		system->saveState(*loadConfig.stateBuffer);

		MemoryAccessor romData = system->getMemory(MemoryType::Rom, AccessType::Read);
		romData.getBuffer().copyTo(loadConfig.romBuffer.get());

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
}
