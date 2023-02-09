#pragma once

#include <string_view>

#include "foundation/DataBuffer.h"
#include "lsdj/LsdjModel.h"
#include "lsdj/Rom.h"
#include "lsdj/LsdjSettings.h"

namespace rp::KitUtil {
	struct SampleData {
		fw::Float32BufferPtr buffer;
		uint32 sampleRate;
	};

	SampleData loadSample(std::string_view path);

	void patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples);

	void updateKit(SystemPtr system, LsdjServiceSettings& settings, KitIndex kitIdx);

	KitIndex addKit(SystemPtr system, LsdjServiceSettings& settings, const std::string& path, KitIndex kitIdx = -1);

	KitIndex addKitSamples(SystemPtr system, LsdjServiceSettings& settings, const std::vector<std::string>& paths, std::string_view name = "", KitIndex kitIdx = -1);
}
