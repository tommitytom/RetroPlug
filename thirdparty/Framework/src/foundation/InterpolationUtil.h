#pragma once

#include <cmath>

#include "foundation/DataBuffer.h"
#include "foundation/MathUtil.h"

namespace fw::InterpolationUtil {
	static f32 interpolateValue(const Float32Buffer& samples, f32 offset) {
		if (samples.size() > 1) {
			offset = MathUtil::clamp(offset, 0.0f, (f32)samples.size() - 1.00001f);

			size_t idx = (size_t)offset;
			f32 frac = fmodf(offset, 1.0f);

			f32 s1 = samples[idx];
			f32 s2 = samples[idx + 1];

			return s1 * (1.0f - frac) + s2 * frac;
		}

		if (samples.size() > 0) {
			return samples.back();
		}

		return 0;
	}
}
