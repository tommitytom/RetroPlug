#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/Random.h"

namespace rp::lsdj {
	namespace SampleUtil {
		const size_t SAMPLES_PER_BYTE_4BIT = 2;

		inline void convertNibblesToF32(const fw::Uint8Buffer& input, fw::Float32Buffer& output) {
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

		// Presumes that input is scaled to [0, 15] range
		inline void convertScaledF32ToNibbles(const fw::Float32Buffer& input, fw::Uint8Buffer& output) {
			const size_t numChunks = input.size() / 32;
			output.resize(numChunks * 16);

			const float* src = input.data();
			uint8_t* dst = output.data();

			for (size_t chunk = 0; chunk < numChunks; ++chunk) {
				uint8_t samples[32];

				// Apply rotation: sample i goes to position (i+1)%32
				samples[0] = 0xF - static_cast<uint8_t>(src[31]);  // Last sample wraps to start
				for (size_t i = 1; i < 32; ++i) {
					samples[i] = 0xF - static_cast<uint8_t>(src[i - 1]);
				}

				// Pack nibbles into bytes
				for (size_t i = 0; i < 16; ++i) {
					*dst++ = (samples[i * 2] << 4) | samples[i * 2 + 1];
				}

				src += 32;
			}
		}

		inline void convertF32ToNibbles(const fw::Float32Buffer& input, fw::Uint8Buffer& output) {
			// Process in chunks of 32 samples (16 bytes when packed)
			const size_t numChunks = input.size() / 32;
			output.resize(numChunks * 16);

			for (size_t chunk = 0; chunk < numChunks; ++chunk) {
				uint8_t samples[32];

				// Convert 32 float samples to 4-bit values
				for (size_t i = 0; i < 32; ++i) {
					float f = input[chunk * 32 + i];

					// Clamp to [-1, 1], then scale to [0, 1]
					f = (std::clamp(f, -1.0f, 1.0f) + 1.0f) * 0.5f;

					// Scale to [0, 15] and round
					uint8_t nibble = static_cast<uint8_t>(std::round(f * 15.0f));

					// Invert and store with rotation
					// Sample i goes to position (i+1)%32
					samples[(i + 1) % 32] = 0xF - nibble;
				}

				// Pack pairs of 4-bit samples into bytes
				for (size_t i = 0; i < 16; ++i) {
					output[chunk * 16 + i] = (samples[i * 2] << 4) | samples[i * 2 + 1];
				}
			}
		}

		/*
		inline void convertF32ToNibbles(const fw::Float32Buffer& input, fw::Uint8Buffer& output) {
			output.resize(input.size() / SAMPLES_PER_BYTE_4BIT);

			int offset = 0;
			size_t addedBytes = 0;
			int outputCounter = 0;
			uint8 outputBuffer[32];

			outputBuffer[0] = 0;

			for (size_t i = 0; i < input.size(); ++i) {
				// Create a clipped sample value between 0 and 1
				f32 s = (std::min(1.0f, std::max(-1.0f, input[i])) + 1.0f) * 0.5f;

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
		}*/
	}
}
