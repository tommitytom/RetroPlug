#pragma once

#include "ecs/RetroPlugComponents.h"
#include "ecs/AudioDithering.h"

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

	struct FilterEffect {
		f32 frequency = 1000.0f;
		f32 q = 0.0f;
		f32 feedback = 0.0f;
	};
	inline void processEffect(const FilterEffect& effect, fw::Float32Buffer& target, f32 sampleRate) {
		// Biquad coefficients
		f32 _b0 = 1.0f, _b1 = 0.0f, _b2 = 0.0f;  // Numerator coefficients
		f32 _a1 = 0.0f, _a2 = 0.0f;              // Denominator coefficients (a0 normalized to 1)

		// Filter state (delay lines)
		f32 _x1 = 0.0f, _x2 = 0.0f;  // Input delay line
		f32 _y1 = 0.0f, _y2 = 0.0f;  // Output delay line
	}

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
