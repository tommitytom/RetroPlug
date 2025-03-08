#pragma once

#include "core/SystemService.h"

namespace rp {
	const uint32 NANOLOOP_SERVICE_TYPE = 0x7470100B;
	struct NanoloopServiceSettings {};

	class NanoloopService final : public TypedSystemService<NanoloopServiceSettings> {
	private:
		bool _romValid = false;
		uint64 _songHash = 0;

	public:
		NanoloopService() : TypedSystemService(NANOLOOP_SERVICE_TYPE) {}
		~NanoloopService() = default;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;
	};
}
