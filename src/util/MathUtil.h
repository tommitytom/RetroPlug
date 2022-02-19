#pragma once

#include <cmath>

namespace rp::MathUtil {
	template <typename T>
	static T clamp(T value, T min, T max) {
		if (value < min) {
			return min;
		}

		if (value > max) {
			return max;
		}

		return value;
	}

	static f32 round(f32 value) {
		return floor(value + 0.5f);
	}
}
