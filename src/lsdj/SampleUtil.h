#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/Random.h"

namespace rp::lsdj {
	namespace SampleUtil {
		const size_t SAMPLES_PER_BYTE_4BIT = 2;

		static void convertNibblesToF32(const fw::Uint8Buffer& input, fw::Float32Buffer& output) {
			output.resize(input.size() * SAMPLES_PER_BYTE_4BIT);

			for (size_t i = 0; i < input.size(); ++i) {
				uint8 n = input[i];
				output[i * 2] = (f32)((n & 0xF0) >> 4);
				output[i * 2 + 1] = (f32)((n & 0xF));
			}

			for (size_t i = 0; i < output.size(); ++i) {
				f32 v = output[i] - 7.0f;
				if (v < 0.0f) {
					v /= 7.0f;
				} else {
					v /= 8.0f;
				}

				output[i] = v;
			}
		}

		static void convertF32ToNibbles(const fw::Float32Buffer& input, fw::Uint8Buffer& output, f32 dither) {
			output.resize(input.size() / SAMPLES_PER_BYTE_4BIT);

			int offset = 0;
			size_t addedBytes = 0;
			int outputCounter = 0;
			uint8 outputBuffer[32];

			outputBuffer[0] = 0;

			f32 halfDither = dither * 0.5f;
			fw::Random ditherRand;
			f32 state = ditherRand.nextFloatRange(-halfDither, halfDither);

			for (size_t i = 0; i < input.size(); ++i) {
				f32 oldState = state;
				state = ditherRand.nextFloatRange(-halfDither, halfDither);
				f32 r = oldState - state;

				// Create a clipped sample value between 0 and 1
				f32 s = (std::min(1.0f, std::max(-1.0f, input[i] + r)) + 1.0f) * 0.5f;

				// Scale value from 0 to 15
				s = floor(s * 15.0f + 0.5f);

				uint8 b = 0xf - (uint8)s;

				// Starting from LSDj 9.2.0, first sample is skipped to compensate for wave refresh bug.
				// This rotates the wave frame rightwards.
				outputBuffer[(outputCounter + 1) % 32] = b;

				if (outputCounter == 31) {
					for (int j = 0; j != 32; j += 2) {
						output[offset++] = (uint8)(outputBuffer[j] * 0x10 + outputBuffer[j + 1]);
					}

					outputCounter = -1;
					addedBytes += 0x10;
				}

				outputCounter++;
			}

			output.resize(addedBytes);
		}
	}
}
