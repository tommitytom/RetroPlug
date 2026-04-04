#pragma once

#include "foundation/Types.h"
#include "foundation/DataBuffer.h"

namespace orb::HashUtil {
	uint64 hash(const Uint8Buffer& buffer, uint64 seed = 0);
}
