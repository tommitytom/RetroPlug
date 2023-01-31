#pragma once

#include <unordered_map>
#include <vector>

#include "System.h"
#include "SystemFactory.h"

namespace rp {
	class SystemManager {
	private:
		std::vector<SystemPtr> _systems;
		std::unordered_map<SystemType, std::pair<SystemProcessorPtr, std::vector<SystemPtr>>> _groupedSystems;
		ConcurrentPoolAllocator<SystemIo>& _ioAllocator;

	public:
		SystemManager(const SystemFactory& factory, ConcurrentPoolAllocator<SystemIo>& ioAllocator);
		~SystemManager() = default;

		void addSystem(SystemPtr system);

		SystemPtr removeSystem(SystemId id);

		void removeAllSystems();

		SystemPtr findSystem(SystemId id);

		void process(uint32 audioFrameCount);

		std::vector<SystemPtr>& getSystems() {
			return _systems;
		}

		const std::vector<SystemPtr>& getSystems() const {
			return _systems;
		}

		std::vector<SystemPtr>& getSystems(SystemType systemType) {
			assert(_groupedSystems.contains(systemType));
			return _groupedSystems[systemType].second;
		}

		/*const std::vector<SystemPtr>& getSystems(SystemType systemType) const {
			assert(_groupedSystems.contains(systemType));
			return _groupedSystems[systemType].second;
		}*/

		void acquireIo(SystemIoPtr&& io) {
			SystemPtr system = findSystem(io->systemId);
			if (system) {
				system->acquireIo(std::move(io));
			}
		}
	};
}
