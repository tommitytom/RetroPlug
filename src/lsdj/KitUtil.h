#pragma once

#include <string_view>

#include "foundation/DataBuffer.h"
#include "lsdj/Rom.h"
#include "lsdj/LsdjSettings.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/SampleCache.h"

namespace rp::KitUtil {
	const uint32 GAMEBOY_SAMPLE_RATE = 11468;

	//SampleData loadSample(std::string_view path);

	//SampleData loadSample(const fw::Uint8Buffer& buffer);

	void patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples);

	bool createKit(SampleCache& sampleCache, lsdj::Kit& kit, const LsdjEditableKit& kitState);

	std::optional<std::string> updateKit2(const LsdjKitComponent& kitState, fw::Uint8Buffer& kitData, SampleCache& sampleCache);

	void updateKit(SystemPtr system, LsdjServiceSettings& settings, KitIndex kitIdx);

	KitIndex addKit(SystemPtr system, LsdjServiceSettings& settings, const std::string& path, KitIndex kitIdx = -1);

	KitIndex addKitSamples(SystemPtr system, LsdjServiceSettings& settings, const std::vector<std::string>& paths, std::string_view name = "", KitIndex kitIdx = -1);

	void convertSamplerate(f64 inputSampleRate, f64 outputSampleRate, const fw::Float32Buffer& buffer, fw::Float32Buffer& target);
}
