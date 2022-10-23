#pragma once

#include <string_view>

#include "foundation/DataBuffer.h"
#include "ui/LsdjModel.h"
#include "lsdj/Rom.h"

namespace rp::KitUtil {
	struct SampleData {
		fw::Float32BufferPtr buffer;
		uint32 sampleRate;
	};

	SampleData loadSample(std::string_view path);

	void patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples);
}
