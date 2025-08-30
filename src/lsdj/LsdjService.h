#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"

namespace rp {
	class LsdjService final : public TypedSystemService<LsdjServiceSettings, LSDJ_SERVICE_TYPE> {
	private:
		bool _romValid = false;
		uint64 _songHash = 0;
		//LsdjRefresher _refresher;

	public:
		LsdjService() = default;
		~LsdjService() = default;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;
	};
}
