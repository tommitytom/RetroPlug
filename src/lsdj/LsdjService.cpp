#include "LsdjService.h"

#include "lsdj/OffsetLookup.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"

namespace rp {
	void LsdjService::onBeforeLoad(LoadConfig& loadConfig) {
		if ((!loadConfig.sramBuffer || loadConfig.sramBuffer->size() == 0) && !loadConfig.stateBuffer) {
			lsdj::Sav sav;
			loadConfig.sramBuffer = std::make_shared<fw::Uint8Buffer>();
			sav.save(*loadConfig.sramBuffer);
		}
	}

	void LsdjService::onAfterLoad(System& system) {
		MemoryAccessor buffer = system.getMemory(MemoryType::Rom, AccessType::Read);
		lsdj::Rom rom(buffer);

		if (rom.isValid()) {
			LsdjServiceSettings& state = getRawState();
			
			state.romValid = true;
			state.offsetsValid = lsdj::OffsetLookup::findOffsets(buffer.getBuffer(), state.ramOffsets, false);

			if (state.offsetsValid) {
				//_refresher.setSystem(system, _ramOffsets);
			} else {
				spdlog::warn("Failed to find ROM offsets");
			}
		}
	}
}
