#pragma once

#include <functional>
#include "audio/TimeInfo.h"

namespace orb::PpqUtil {
	void eachTick(const orb::TimeInfo& time, uint32 resolution, std::function<void(uint32 ppq, uint32 offset)>&& func);
}
