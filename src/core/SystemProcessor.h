#pragma once

#include "core/SystemManager.h"

namespace rp {
	class ProxySystemBase;
	using ProxySystemPtr = std::shared_ptr<ProxySystemBase>;

	class AudioStreamSystem;
	using AudioStreamSystemPtr = std::shared_ptr<AudioStreamSystem>;

	class SystemProcessor {
	private:
		std::vector<SystemManagerPtr> _managers;
		std::vector<SystemPtr> _systems;
		std::vector<SystemPtr> _ungroupedSystems;

	public:
		template <typename T>
		void addManager() {
			_managers.push_back(std::make_shared<T>());
		}

		void addManager(SystemManagerPtr manager) {
			_managers.push_back(manager);
		}

		void addSystem(SystemPtr system);

		SystemPtr removeSystem(SystemId id);

		void removeAllSystems();

		SystemPtr findSystem(SystemId id);

		void process(uint32 frameCount);

		std::vector<SystemPtr>& getSystems() {
			return _systems;
		}

		std::vector<SystemPtr>& getSystems(SystemType systemType) {
			SystemManagerBase* manager = findManager(systemType);
			assert(manager);

			return manager->getSystems();
		}

		template <typename T>
		std::vector<SystemPtr>& getSystems() {
			return getSystems(entt::type_id<T>().seq());
		}

		std::vector<SystemType> getRomLoaders(std::string_view path);

		std::vector<SystemType> getSramLoaders(std::string_view path);

		SystemManagerBase* findManager(SystemType type);
		
		template <typename T>
		SystemManagerBase* findManager() {
			return findManager(entt::type_id<T>().seq());
		}
	};
}
