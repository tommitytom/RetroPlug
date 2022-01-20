#pragma once

#include "core/AudioState.h"
//#include "core/Proxies.h"
#include "util/DataBuffer.h"
#include "SystemManager.h"

namespace rp {
	class ProxySystemBase : public System<ProxySystemBase> {
	private:
		SystemType _targetType;

	public:
		ProxySystemBase(SystemId id): System<ProxySystemBase>(id) {}

		void setDesc(SystemType targetType) {
			_targetType = targetType;
		}

		virtual void handleSetup(SystemPtr& system) = 0;
	};

	template <typename T>
	class ProxySystem : public ProxySystemBase {
	private:
		std::string _romName;

	public:
		ProxySystem(SystemId id): ProxySystemBase(id) {}

		virtual void setup(T& system) {}

		void handleSetup(SystemPtr& system) final override {
			_romName = system->getRomName();

			setDesc(system->getType());
			setup(*std::static_pointer_cast<T>(system));
		}

		std::string getRomName() override {
			return _romName;
		}
	};

	using ProxySystemPtr = std::shared_ptr<ProxySystemBase>;

	/*class ProxyManager : public SystemManager<ProxySystemBase> {
	public:
		ProxyManager() {}

		bool canLoadRom(std::string_view path) override { return false; }

		bool canLoadSram(std::string_view path) override { return false; }
	};*/
}
