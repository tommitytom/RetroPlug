#pragma once

#include <functional>
#include "audio/TimeInfo.h"

namespace fw::PpqUtil {
	void eachTick(const fw::TimeInfo& time, uint32 resolution, std::function<void(uint32 ppq, uint32 offset)>&& func);
}
