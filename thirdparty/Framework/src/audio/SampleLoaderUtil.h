#pragma once

#include <string_view>
#include "foundation/DataBuffer.h"

namespace fw::SampleLoaderUtil {
	struct SampleData {
		Float32BufferPtr buffer;
		uint32 sampleRate;
	};

	SampleData loadSample(std::string_view path);
}
