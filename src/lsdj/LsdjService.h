#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class LsdjService final : public TypedSystemService<LsdjServiceSettings> {
	private:
		bool _romValid = false;
		uint64 _songHash = 0;
		//LsdjRefresher _refresher;
		
	public:
		LsdjService() : TypedSystemService(LSDJ_SERVICE_TYPE) {}
		~LsdjService() = default;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;
	};
}
