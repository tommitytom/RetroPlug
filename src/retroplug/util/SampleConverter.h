#pragma once

namespace SampleConverter {
	void s16_to_f32(float* target, int16_t* source, size_t count) {
		for (size_t i = 0; i < count; ++i) {
			float x = (float)source[i];
			target[i] = ((x + 32768.0f) * 0.00003051804379339284f) - 1;
		}
	}
}
