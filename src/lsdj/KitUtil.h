#pragma once

#include <string_view>

#include "foundation/DataBuffer.h"
#include "lsdj/Rom.h"
#include "lsdj/LsdjSettings.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/SampleCache.h"

namespace rp::KitUtil {
	bool createKit(SampleCache& sampleCache, lsdj::Kit& kit, const LsdjEditableKit& kitState);

	std::optional<std::string> updateKit2(const LsdjKitComponent& kitState, fw::Uint8Buffer& kitData, SampleCache& sampleCache);

	void convertSamplerate(f64 inputSampleRate, f64 outputSampleRate, const fw::Float32Buffer& buffer, fw::Float32Buffer& target);
}
