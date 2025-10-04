#pragma once

#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

#include "foundation/DataBuffer.h"
#include "foundation/Types.h"

namespace rp {
	class AudioDithering {
	private:
		std::mt19937 _rng;
		std::uniform_real_distribution<f32> _dist;

	public:
		AudioDithering() : _rng(std::random_device{}()), _dist(0.0f, 1.0f) {}

		// Error Diffusion Dithering (Floyd-Steinberg style)
		// Input: normalized f32 samples [-1, 1]
		// Output: f32 samples quantized to [0, 2^bitDepth - 1] levels
		void errorDiffusion(fw::Float32Buffer& buffer, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;  // 2^bitDepth
			const f32 maxValue = static_cast<f32>(levels - 1);
			const f32 halfScale = maxValue / 2.0f;
			f32* bufferData = buffer.data();

			f32 error = 0.0f;

			for (size_t i = 0; i < buffer.size(); ++i) {
				// Convert from [-1, 1] to [0, maxValue]
				f32 value = (bufferData[i] + 1.0f) * halfScale;

				// Add diffused error from previous sample
				value += error * 0.9f;

				// Clamp to valid range
				value = std::clamp(value, 0.0f, maxValue);

				// Quantize to nearest level
				f32 quantized = std::round(value);

				// Calculate error for next sample
				error = value - quantized;

				// Output the quantized f32 value
				bufferData[i] = quantized;
			}
		}

		// Sierra Lite Error Diffusion (lighter weight variant)
		void sierraLiteErrorDiffusion(fw::Float32Buffer& input, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;
			const f32 maxValue = static_cast<f32>(levels - 1);
			const f32 halfScale = maxValue / 2.0f;
			const size_t inputSize = input.size();

			fw::Float32Buffer paddedBuffer(inputSize + 2);
			f32* inputData = input.data();
			f32* paddedData = paddedBuffer.data();

			// Copy input to buffer with padding, converting to [0, maxValue] range
			for (size_t i = 0; i < inputSize; ++i) {
				paddedData[i] = (inputData[i] + 1.0f) * halfScale;
			}

			for (size_t i = 0; i < inputSize; ++i) {
				f32 value = paddedData[i];

				// Quantize
				f32 quantized = std::round(std::clamp(value, 0.0f, maxValue));
				inputData[i] = quantized;

				// Calculate and distribute error
				f32 error = value - quantized;

				// Sierra Lite distribution pattern:
				// *   2/4
				// 1/4 1/4
				if (i + 1 < inputSize) {
					paddedData[i + 1] += error * 0.5f;  // 2/4 to the right
				}
				if (i + 2 < inputSize) {
					paddedData[i + 2] += error * 0.25f; // 1/4 to the next right
				}
			}
		}

		// High-Pass TPDF (Triangular Probability Density Function) Dithering
		void highPassTPDF(fw::Float32Buffer& input, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;
			const f32 maxValue = static_cast<f32>(levels - 1);
			const f32 halfScale = maxValue / 2.0f;
			const size_t inputSize = input.size();

			f32* inputData = input.data();

			// Quantum step size for dithering
			const f32 quantum = 1.0f;  // 1 LSB in the quantized domain

			f32 prevDither = 0.0f;
			f32 shapingState = 0.0f;

			for (size_t i = 0; i < inputSize; ++i) {
				// Generate triangular dither (sum of two uniform distributions)
				// Scale to ±0.5 LSB for optimal dithering
				f32 dither = (_dist(_rng) + _dist(_rng) - 1.0f) * quantum * 0.5f;

				// Apply first-order high-pass filtering to shape the noise
				f32 shapedDither = dither - prevDither * 0.94f;
				prevDither = dither;

				// Additional noise shaping feedback
				shapedDither -= shapingState * 0.5f;

				// Convert input to [0, maxValue] range and add shaped dither
				f32 value = (inputData[i] + 1.0f) * halfScale + shapedDither;

				// Clamp and quantize
				value = std::clamp(value, 0.0f, maxValue);
				f32 quantized = std::round(value);

				// Update shaping state for next sample
				shapingState = quantized - value;

				inputData[i] = quantized;
			}
		}

		// Shaped TPDF with 2nd order noise shaping
		void shapedTPDF2ndOrder(fw::Float32Buffer& input, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;
			const f32 maxValue = static_cast<f32>(levels - 1);
			const f32 halfScale = maxValue / 2.0f;
			const f32 quantum = 1.0f;
			const size_t inputSize = input.size();

			f32* inputData = input.data();

			f32 error1 = 0.0f;
			f32 error2 = 0.0f;

			for (size_t i = 0; i < inputSize; ++i) {
				// Generate TPDF dither
				f32 dither = (_dist(_rng) + _dist(_rng) - 1.0f) * quantum * 0.5f;

				// Convert input to [0, maxValue] range
				f32 value = (input[i] + 1.0f) * halfScale;

				// Apply 2nd order noise shaping
				// Coefficients optimized for 11kHz sample rate
				value += dither + (1.623f * error1) - (0.982f * error2);

				// Clamp and quantize
				value = std::clamp(value, 0.0f, maxValue);
				f32 quantized = std::round(value);

				// Update error history
				f32 quantError = quantized - value;
				error2 = error1;
				error1 = quantError;

				inputData[i] = quantized;
			}
		}

		// Alternative: Jarvis-Judice-Ninke error diffusion
		// Distributes error across more samples for smoother results
		void jjnErrorDiffusion(fw::Float32Buffer& input, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;
			const f32 maxValue = static_cast<f32>(levels - 1);
			const f32 halfScale = maxValue / 2.0f;
			f32* inputData = input.data();

			// Create working buffer with padding
			const size_t width = input.size();
			fw::Float32Buffer buffer(width + 4);
			f32* bufferData = buffer.data();

			// Convert input to [0, maxValue] range
			for (size_t i = 0; i < width; ++i) {
				bufferData[i] = (input[i] + 1.0f) * halfScale;
			}

			// JJN error distribution weights (divided by 48)
			const f32 w1 = 7.0f / 48.0f;
			const f32 w2 = 5.0f / 48.0f;
			const f32 w3 = 3.0f / 48.0f;
			const f32 w4 = 1.0f / 48.0f;

			for (size_t i = 0; i < width; ++i) {
				f32 value = bufferData[i];

				// Quantize
				f32 quantized = std::round(std::clamp(value, 0.0f, maxValue));
				inputData[i] = quantized;

				// Calculate error
				f32 error = value - quantized;

				// Distribute error using JJN pattern
				//         *   7   5
				// 3   5   7   5   3
				// 1   3   5   3   1
				if (i + 1 < width) bufferData[i + 1] += error * w1;
				if (i + 2 < width) bufferData[i + 2] += error * w2;
				if (i + 3 < width) bufferData[i + 3] += error * w3;
				if (i + 4 < width) bufferData[i + 4] += error * w4;
			}
		}

		// Helper function to convert quantized values back to normalized f32 [-1, 1]
		void toNormalized(fw::Float32Buffer& buffer, int32 bitDepth = 4) {
			const int32 levels = 1 << bitDepth;
			const f32 maxValue = static_cast<f32>(levels - 1);
			const size_t size = buffer.size();
			f32* data = buffer.data();

			for (size_t i = 0; i < size; ++i) {
				// Convert from [0, maxValue] to [-1, 1]
				data[i] = (data[i] / maxValue) * 2.0f - 1.0f;
			}
		}
	};
}
