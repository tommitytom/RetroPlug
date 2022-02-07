#include "SystemProcessor.h"

#include <entt/core/type_info.hpp>

#include "core/ProxySystem.h"
#include "core/AudioStreamSystem.h"

using namespace rp;

void SystemProcessor::addSystem(SystemPtr system) {
	_systems.push_back(system);

	SystemManagerBase* manager = findManager(system->getBaseType());
	if (manager) {
		manager->add(system);
	} else {
		_ungroupedSystems.push_back(system);
	}
}

SystemPtr SystemProcessor::removeSystem(SystemId id) {
	SystemPtr found;
	for (SystemManagerPtr& manager : _managers) {
		found = manager->remove(id);
		if (found) {
			break;
		}
	}

	for (size_t i = 0; i < _systems.size(); ++i) {
		if (_systems[i]->getId() == id) {
			_systems.erase(_systems.begin() + i);
			break;
		}
	}

	for (size_t i = 0; i < _ungroupedSystems.size(); ++i) {
		if (_ungroupedSystems[i]->getId() == id) {
			_ungroupedSystems.erase(_ungroupedSystems.begin() + i);
			break;
		}
	}

	return found;
}

void SystemProcessor::removeAllSystems() {
	std::vector<SystemId> ids;

	for (auto& system : _systems) {
		ids.push_back(system->getId());
	}

	for (SystemId systemId : ids) {
		removeSystem(systemId);
	}
}

SystemPtr SystemProcessor::findSystem(SystemId id) {
	for (SystemPtr& system : _systems) {
		if (system->getId() == id) {
			return system;
		}
	}

	return nullptr;
}

std::vector<SystemType> SystemProcessor::getRomLoaders(std::string_view path) {
	std::vector<SystemType> ret;

	for (SystemManagerPtr& manager : _managers) {
		if (manager->canLoadRom(path)) {
			ret.push_back(manager->getSystemType());
		}
	}

	return ret;
}

std::vector<SystemType> SystemProcessor::getSramLoaders(std::string_view path) {
	std::vector<SystemType> ret;

	for (SystemManagerPtr& manager : _managers) {
		if (manager->canLoadSram(path)) {
			ret.push_back(manager->getSystemType());
		}
	}

	return ret;
}

void SystemProcessor::process(uint32 frameCount) {
	for (SystemPtr& system : _systems) {
		system->processStateCopy(frameCount);
	}

	for (SystemPtr& system : _ungroupedSystems) {
		system->process(frameCount);
	}

	for (SystemManagerPtr& manager : _managers) {
		manager->process(frameCount);
	}
}

SystemManagerBase* SystemProcessor::findManager(SystemType type) {
	for (SystemManagerPtr& manager : _managers) {
		if (manager->getSystemType() == type) {
			return manager.get();
		}
	}

	return nullptr;
}
