#pragma once

#include <cmath>

#include "foundation/DataBuffer.h"
#include "foundation/MathUtil.h"

namespace fw::InterpolationUtil {
	static f32 interpolateValue(const f32* values, size_t valueCount, f32 offset, size_t stride = 1) {
		size_t sampleCount = valueCount * stride;

		if (valueCount > 1) {
			offset = MathUtil::clamp(offset, 0.0f, (f32)valueCount - 1.0f);

			size_t idx1 = (size_t)offset * stride;
			size_t idx2 = idx1 + stride;

			f32 s1 = values[idx1];
			f32 s2 = idx2 < sampleCount ? values[idx2] : 0;

			f32 frac = fmodf(offset, 1.0f);

			return s1 * (1.0f - frac) + s2 * frac;
		}

		if (valueCount == 1) {
			return values[valueCount - 1];
		}

		return 0;
	}

	static f32 interpolateValue(const Float32Buffer& samples, f32 offset, size_t stride = 1) {
		return interpolateValue(samples.data(), samples.size(), offset, stride);
	}
}
