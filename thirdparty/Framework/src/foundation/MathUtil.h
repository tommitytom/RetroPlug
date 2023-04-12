#pragma once

#include <cmath>
#include "foundation/Types.h"

namespace fw::MathUtil {
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

	static f64 round(f64 value) {
		return floor(value + 0.5);
	}

	static uint8 reverse(uint8 b) {
		b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
		b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
		b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
		return b;
	}
}
