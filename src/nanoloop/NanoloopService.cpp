#include "NanoloopService.h"

namespace rp {
	void NanoloopService::onBeforeLoad(LoadConfig& loadConfig) {
		// Nanoloop does not support SRAM
		loadConfig.sramBuffer = nullptr;
		loadConfig.desc.settings.saveType = SaveStateType::State;
	}

	void NanoloopService::onAfterLoad(System& system) {
	}
}
