#pragma once

#include <string_view>
#include <vector>

#include <entt/core/type_info.hpp>

#include "System.h"

namespace rp {
	class ProxySystemBase;
	using ProxySystemPtr = std::shared_ptr<ProxySystemBase>;

	class SystemManagerBase {
	private:
		SystemType _systemType;
		std::vector<SystemPtr> _systems;

	public:
		SystemManagerBase(SystemType systemType) : _systemType(systemType) {}

		virtual SystemPtr create(SystemId id) = 0;

		virtual ProxySystemPtr createProxy(SystemId id) = 0;

		virtual bool canLoadRom(std::string_view path) { return false; }

		virtual bool canLoadSram(std::string_view path) { return false; }

		virtual std::string getRomName(const fw::Uint8Buffer& romData) { return ""; }

		void add(SystemPtr system) {
			assert(system->getType() == _systemType);
			_systems.push_back(system);
		}

		SystemPtr find(SystemId id) const {
			for (const SystemPtr& system : _systems) {
				if (system->getId() == id) {
					return system;
				}
			}

			return nullptr;
		}

		SystemPtr remove(SystemId id) {
			for (size_t i = 0; i < _systems.size(); ++i) {
				SystemPtr system = _systems[i];

				if (system->getId() == id) {
					_systems.erase(_systems.begin() + i);
					return system;
				}
			}

			return nullptr;
		}		

		SystemType getSystemType() const {
			return _systemType;
		}

		std::vector<SystemPtr>& getSystems() {
			return _systems;
		}

		virtual void process(uint32 frameCount) {
			for (SystemPtr& system : _systems) {
				system->process(frameCount);
			}
		}

		template <typename SystemType, typename ProxyType> friend class SystemManager;
	};

	template <typename SystemType, typename ProxyType = void>
	class SystemManager : public SystemManagerBase {
	public:
		SystemManager() : SystemManagerBase(entt::type_id<SystemType>().index()) {}

		SystemPtr create(SystemId id) final override {
			return std::make_shared<SystemType>(id);
		}

		ProxySystemPtr createProxy(SystemId id) final override {
			if constexpr (!std::is_same_v<ProxyType, void>) {
				return std::make_shared<ProxyType>(id);
			}

			return nullptr;
		}
	};

	using SystemManagerPtr = std::shared_ptr<SystemManagerBase>;
}
