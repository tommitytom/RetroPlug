#pragma once

#include "foundation/Input.h"

namespace rp::Ps2Util {
	int writeExtended(fw::VirtualKey vk, uint8_t* target);

	int getMakeCode(fw::VirtualKey vk, uint8_t* target, bool includeExt);

	int getBreakCode(fw::VirtualKey vk, uint8_t* target);
}
