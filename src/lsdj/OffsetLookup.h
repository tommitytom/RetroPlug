#pragma once

#include "foundation/Types.h"
#include "sameboy/semver.hpp"
#include "lsdj/Ram.h"

namespace rp::lsdj {
	struct RomInfo {
		std::string name;
		semver::version version{0, 0, 0};
		std::string tags;
		uint64 hash = 0;
		bool isStock = false;
	};

	namespace OffsetLookup {
		bool findOffsets(const fw::Uint8Buffer& romData, MemoryOffsets& offsets, bool forceCalculate = false);

		RomInfo getRomInfo(const fw::Uint8Buffer& romData);
	}
}
