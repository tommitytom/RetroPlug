#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class LsdjService final : public SystemService {
	private:
		lsdj::MemoryOffsets _ramOffsets;
		bool _offsetsValid = false;
		bool _romValid = false;
		uint64 _songHash = 0;
		//LsdjRefresher _refresher;
		
		LsdjServiceSettings _state;
		
	public:
		LsdjService() : SystemService(LSDJ_SERVICE_TYPE) {}
		~LsdjService() = default;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;

		void setState(const entt::any& data) override {
			_state = entt::any_cast<const LsdjServiceSettings&>(data);
		}

		void setState(entt::any&& data) override {
			_state = std::move(entt::any_cast<LsdjServiceSettings&>(data));
		}

		const entt::any getState() const override {
			return entt::forward_as_any(_state);
		}

		entt::any getState() override {
			return entt::forward_as_any(_state);
		}
	};
}
