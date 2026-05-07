#pragma once

#include "foundation/DataBuffer.h"

namespace orb::AudioLoaderUtil {
	bool load(std::string_view path, Float32Buffer& target);
}
