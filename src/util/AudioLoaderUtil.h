#pragma once

#include "util/DataBuffer.h"

namespace rp::AudioLoaderUtil {
	bool load(std::string_view path, Float32Buffer& target);
}
