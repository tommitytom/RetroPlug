#include "LsdjService.h"

#include "lsdj/OffsetLookup.h"
#include "lsdj/Ram.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"

namespace rp {
	void LsdjService::onBeforeLoad(LoadConfig& loadConfig) {
		if ((!loadConfig.sramBuffer || loadConfig.sramBuffer->size() == 0) && !loadConfig.stateBuffer) {
			// LSDj has to initialize the SRAM if no save data is available when it starts
			// Create an SRAM buffer from an empty save to skip this init step

			loadConfig.sramBuffer = std::make_shared<fw::Uint8Buffer>();

			lsdj::Sav sav;
			sav.save(*loadConfig.sramBuffer);
		}
	}

	void LsdjService::onAfterLoad(System& system) {
		const MemoryAccessor buffer = system.getMemory(MemoryType::Rom, AccessType::Read);
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
