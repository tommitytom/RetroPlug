#pragma once

#include <string_view>

#include "foundation/DataBuffer.h"
#include "lsdj/LsdjModel.h"
#include "lsdj/Rom.h"
#include "lsdj/LsdjSettings.h"

namespace rp::KitUtil {
	const uint32 GAMEBOY_SAMPLE_RATE = 11468;

	struct SampleData {
		std::string name;
		fw::Float32BufferPtr buffer;
		uint32 sampleRate;
	};

	SampleData loadSample(std::string_view path);

	SampleData loadSample(const fw::Uint8Buffer& buffer);

	void patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples);

	void updateKit(SystemPtr system, LsdjServiceSettings& settings, KitIndex kitIdx);

	KitIndex addKit(SystemPtr system, LsdjServiceSettings& settings, const std::string& path, KitIndex kitIdx = -1);

	KitIndex addKitSamples(SystemPtr system, LsdjServiceSettings& settings, const std::vector<std::string>& paths, std::string_view name = "", KitIndex kitIdx = -1);

	void convertSamplerate(f64 inputSampleRate, f64 outputSampleRate, const fw::Float32Buffer& buffer, fw::Float32Buffer& target);
}
