#pragma once

#include "foundation/DataBuffer.h"

namespace fw::AudioLoaderUtil {
	bool load(std::string_view path, Float32Buffer& target);
}
