#pragma once

#include "platform/Types.h"
#include "sameboy/semver.hpp"
#include "lsdj/Ram.h"

namespace rp::lsdj {
	namespace OffsetLookup {
		bool findOffsets(const Uint8Buffer& romData, MemoryOffsets& offsets, bool forceCalculate = false);
	}
}
