#pragma once

#include <rfl/TaggedUnion.hpp>
#include "ecs/AudioDithering.h"
#include "util/GameboyUtil.h"

namespace rp {
	struct GainEffect {
		f32 gain = 1.0f;
	};
	inline void processEffect(const GainEffect& effect, fw::Float32Buffer& target, f32 sampleRate) {
		f32* data = target.data();
		size_t size = target.size();
		for (size_t i = 0; i < size; ++i) {
			data[i] *= effect.gain;
		}
	}

	enum class FilterType {
		LowPass,
		HighPass,
		BandPass,
		BandStop,
		Peak,
		LowShelf,
		HighShelf,
		AllPass
	};	
	struct FilterEffect {
		FilterType filterType = FilterType::LowPass;
		f32 frequency = GameboyUtil::GAMEBOY_SAMPLE_RATE / 2.0f;
		f32 q = 0.0f;
		f32 feedback = 0.0f;
		f32 gain = 0.0f;
	};
	void processEffect(const FilterEffect& effect, fw::Float32Buffer& target, f32 sampleRate);

	struct DitherEffect {
		enum class Type {
			ErrorDiffusion,
			SierraLite,
			JJN,
			HighPassTPDF,
			ShapedTPDF
		} ditherType = Type::ErrorDiffusion;
	};
	inline void processEffect(const DitherEffect& effect, fw::Float32Buffer& target, f32 sampleRate) {
		AudioDithering dither;

		switch (effect.ditherType) {
			case DitherEffect::Type::ErrorDiffusion:
				dither.errorDiffusion(target, 4);
				break;
			case DitherEffect::Type::SierraLite:
				dither.sierraLiteErrorDiffusion(target, 4);
				break;
			case DitherEffect::Type::JJN:
				dither.jjnErrorDiffusion(target, 4);
				break;
			case DitherEffect::Type::HighPassTPDF:
				dither.highPassTPDF(target, 4);
				break;
			case DitherEffect::Type::ShapedTPDF:
				dither.shapedTPDF2ndOrder(target, 4);
				break;
		}
	}

	using LsdjEffect = rfl::TaggedUnion<"type", GainEffect, FilterEffect, DitherEffect>;
}
