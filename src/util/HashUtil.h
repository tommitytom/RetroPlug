#pragma once

#include "platform/Types.h"
#include "util/DataBuffer.h"

namespace rp::HashUtil {
	uint64 hash(const Uint8Buffer& buffer, uint64 seed = 0);
}
