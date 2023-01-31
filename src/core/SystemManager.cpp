#include "SystemManager.h"

#include "core/System.h"
#include "core/SystemProcessor.h"
#include "core/SystemProvider.h"

namespace rp {
	SystemManager::SystemManager(const SystemFactory& factory, ConcurrentPoolAllocator<SystemIo>& ioAllocator): _ioAllocator(ioAllocator) {
		for (const SystemProviderPtr& provider : factory.getSystemProviders()) {
			_groupedSystems[provider->getType()] = {
				provider->createProcessor(),
				std::vector<SystemPtr>()
			};
		}
	}

	void SystemManager::addSystem(SystemPtr system) {
		assert(!findSystem(system->getId()));
		assert(_groupedSystems.contains(system->getType()));

		_systems.push_back(system);
		_groupedSystems[system->getType()].second.push_back(system);
	}

	SystemPtr SystemManager::removeSystem(SystemId id) {
		SystemPtr found = findSystem(id);
		if (!found) {
			return nullptr;
		}

		for (size_t i = 0; i < _systems.size(); ++i) {
			if (_systems[i]->getId() == id) {
				_systems.erase(_systems.begin() + i);
				break;
			}
		}

		for (auto& [type, systems] : _groupedSystems) {
			for (size_t i = 0; i < systems.second.size(); ++i) {
				if (systems.second[i]->getId() == id) {
					systems.second.erase(systems.second.begin() + i);
					break;
				}
			}
		}

		return found;
	}

	void SystemManager::removeAllSystems() {
		_systems.clear();

		for (auto&& [k, v] : _groupedSystems) {
			v.second.clear();
		}
	}

	SystemPtr SystemManager::findSystem(SystemId id) {
		for (SystemPtr& system : _systems) {
			if (system->getId() == id) {
				return system;
			}
		}

		return nullptr;
	}

	void SystemManager::process(uint32 audioFrameCount) {
		for (SystemPtr& system : _systems) {
			assert(system->hasIo());
		}

		for (auto& [type, systems] : _groupedSystems) {
			if (systems.first) {
				systems.first->process(systems.second, audioFrameCount);
			} else {
				for (SystemPtr& system : systems.second) {
					system->process(audioFrameCount);
				}
			}			
		}
	}
}
