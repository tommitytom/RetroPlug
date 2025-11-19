#pragma once

#include "foundation/Input.h"

namespace rp::Ps2Util {
	int writeExtended(orb::VirtualKey vk, uint8_t* target);

	int getMakeCode(orb::VirtualKey vk, uint8_t* target, bool includeExt);

	int getBreakCode(orb::VirtualKey vk, uint8_t* target);
}
