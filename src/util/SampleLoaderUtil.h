#pragma once

#include <string_view>
#include "util/DataBuffer.h"

namespace rp::SampleLoaderUtil {
	struct SampleData {
		Float32BufferPtr buffer;
		uint32 sampleRate;
	};

	SampleData loadSample(std::string_view path);
}
