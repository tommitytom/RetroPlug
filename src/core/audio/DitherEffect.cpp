#include "DitherEffect.h"
#include <cassert>

namespace rp {

DitherEffect::DitherEffect()
	: _rng(std::random_device{}()), _dist(0.0f, 1.0f) {
	reset();
}

void DitherEffect::process(fw::AudioBuffer& buffer) {
	if (!_enabled || buffer.isEmpty()) {
		return;
	}

	const uint32 channelCount = buffer.getChannelCount();
	const uint32 sampleCount = buffer.getSampleCount();

	// Process each channel independently
	for (uint32 ch = 0; ch < channelCount; ++ch) {
		float* samples = buffer.getWritePointer(ch);

		switch (_mode) {
			case DitherMode::ErrorDiffusion:
				processErrorDiffusion(samples, sampleCount);
				break;
			case DitherMode::SierraLite:
				processSierraLite(samples, sampleCount);
				break;
			case DitherMode::HighPassTPDF:
				processHighPassTPDF(samples, sampleCount);
				break;
			case DitherMode::ShapedTPDF2ndOrder:
				processShapedTPDF2ndOrder(samples, sampleCount);
				break;
			case DitherMode::JJNErrorDiffusion:
				processJJNErrorDiffusion(samples, sampleCount);
				break;
		}
	}
}

void DitherEffect::reset() {
	_errorDiffusionError = 0.0f;
	_prevDither = 0.0f;
	_shapingState = 0.0f;
	_error1 = 0.0f;
	_error2 = 0.0f;
	_workingBuffer.clear();
}

void DitherEffect::configureErrorDiffusion(int bitDepth) {
	_mode = DitherMode::ErrorDiffusion;
	_bitDepth = std::clamp(bitDepth, 1, 16);
	reset();
}

void DitherEffect::configureSierraLite(int bitDepth) {
	_mode = DitherMode::SierraLite;
	_bitDepth = std::clamp(bitDepth, 1, 16);
	reset();
}

void DitherEffect::configureHighPassTPDF(int bitDepth) {
	_mode = DitherMode::HighPassTPDF;
	_bitDepth = std::clamp(bitDepth, 1, 16);
	reset();
}

void DitherEffect::configureShapedTPDF2ndOrder(int bitDepth) {
	_mode = DitherMode::ShapedTPDF2ndOrder;
	_bitDepth = std::clamp(bitDepth, 1, 16);
	reset();
}

void DitherEffect::configureJJNErrorDiffusion(int bitDepth) {
	_mode = DitherMode::JJNErrorDiffusion;
	_bitDepth = std::clamp(bitDepth, 1, 16);
	reset();
}

void DitherEffect::processErrorDiffusion(float* samples, uint32 sampleCount) {
	const int levels = 1 << _bitDepth;  // 2^bitDepth
	const float maxValue = static_cast<float>(levels - 1);
	const float halfScale = maxValue / 2.0f;

	for (uint32 i = 0; i < sampleCount; ++i) {
		// Convert from [-1, 1] to [0, maxValue]
		float value = (samples[i] + 1.0f) * halfScale;

		// Add diffused error from previous sample
		value += _errorDiffusionError * 0.9f;

		// Clamp to valid range
		value = std::clamp(value, 0.0f, maxValue);

		// Quantize to nearest level
		float quantized = quantizeValue(value, maxValue);

		// Calculate error for next sample
		_errorDiffusionError = value - quantized;

		// Convert back to [-1, 1] and store
		samples[i] = (quantized / maxValue) * 2.0f - 1.0f;
	}
}

void DitherEffect::processSierraLite(float* samples, uint32 sampleCount) {
	const int levels = 1 << _bitDepth;
	const float maxValue = static_cast<float>(levels - 1);
	const float halfScale = maxValue / 2.0f;

	// Resize working buffer if needed
	if (_workingBuffer.size() < sampleCount + 2) {
		_workingBuffer.resize(sampleCount + 2, 0.0f);
	}

	// Copy input to buffer with padding, converting to [0, maxValue] range
	for (uint32 i = 0; i < sampleCount; ++i) {
		_workingBuffer[i] = (samples[i] + 1.0f) * halfScale;
	}
	_workingBuffer[sampleCount] = 0.0f;
	_workingBuffer[sampleCount + 1] = 0.0f;

	for (uint32 i = 0; i < sampleCount; ++i) {
		float value = _workingBuffer[i];

		// Quantize
		float quantized = quantizeValue(std::clamp(value, 0.0f, maxValue), maxValue);

		// Calculate and distribute error
		float error = value - quantized;

		// Sierra Lite distribution pattern:
		// *   2/4
		// 1/4 1/4
		if (i + 1 < sampleCount) {
			_workingBuffer[i + 1] += error * 0.5f;  // 2/4 to the right
		}
		if (i + 2 < sampleCount) {
			_workingBuffer[i + 2] += error * 0.25f; // 1/4 to the next right
		}

		// Convert back to [-1, 1] and store
		samples[i] = (quantized / maxValue) * 2.0f - 1.0f;
	}
}

void DitherEffect::processHighPassTPDF(float* samples, uint32 sampleCount) {
	const int levels = 1 << _bitDepth;
	const float maxValue = static_cast<float>(levels - 1);
	const float halfScale = maxValue / 2.0f;

	// Quantum step size for dithering
	const float quantum = 1.0f;  // 1 LSB in the quantized domain

	for (uint32 i = 0; i < sampleCount; ++i) {
		// Generate triangular dither (sum of two uniform distributions)
		// Scale to ±0.5 LSB for optimal dithering
		float dither = generateTPDFDither() * quantum * 0.5f;

		// Apply first-order high-pass filtering to shape the noise
		float shapedDither = dither - _prevDither * 0.94f;
		_prevDither = dither;

		// Additional noise shaping feedback
		shapedDither -= _shapingState * 0.5f;

		// Convert input to [0, maxValue] range and add shaped dither
		float value = (samples[i] + 1.0f) * halfScale + shapedDither;

		// Clamp and quantize
		value = std::clamp(value, 0.0f, maxValue);
		float quantized = quantizeValue(value, maxValue);

		// Update shaping state for next sample
		_shapingState = quantized - value;

		// Convert back to [-1, 1] and store
		samples[i] = (quantized / maxValue) * 2.0f - 1.0f;
	}
}

void DitherEffect::processShapedTPDF2ndOrder(float* samples, uint32 sampleCount) {
	const int levels = 1 << _bitDepth;
	const float maxValue = static_cast<float>(levels - 1);
	const float halfScale = maxValue / 2.0f;
	const float quantum = 1.0f;

	for (uint32 i = 0; i < sampleCount; ++i) {
		// Generate TPDF dither
		float dither = generateTPDFDither() * quantum * 0.5f;

		// Convert input to [0, maxValue] range
		float value = (samples[i] + 1.0f) * halfScale;

		// Apply 2nd order noise shaping
		// Coefficients optimized for 11kHz sample rate
		value += dither + (1.623f * _error1) - (0.982f * _error2);

		// Clamp and quantize
		value = std::clamp(value, 0.0f, maxValue);
		float quantized = quantizeValue(value, maxValue);

		// Update error history
		float quantError = quantized - value;
		_error2 = _error1;
		_error1 = quantError;

		// Convert back to [-1, 1] and store
		samples[i] = (quantized / maxValue) * 2.0f - 1.0f;
	}
}

void DitherEffect::processJJNErrorDiffusion(float* samples, uint32 sampleCount) {
	const int levels = 1 << _bitDepth;
	const float maxValue = static_cast<float>(levels - 1);
	const float halfScale = maxValue / 2.0f;

	// Resize working buffer if needed
	if (_workingBuffer.size() < sampleCount + 4) {
		_workingBuffer.resize(sampleCount + 4, 0.0f);
	}

	// Convert input to [0, maxValue] range
	for (uint32 i = 0; i < sampleCount; ++i) {
		_workingBuffer[i] = (samples[i] + 1.0f) * halfScale;
	}
	// Clear padding
	for (uint32 i = sampleCount; i < sampleCount + 4; ++i) {
		_workingBuffer[i] = 0.0f;
	}

	// JJN error distribution weights (divided by 48)
	const float w1 = 7.0f / 48.0f;
	const float w2 = 5.0f / 48.0f;
	const float w3 = 3.0f / 48.0f;
	const float w4 = 1.0f / 48.0f;

	for (uint32 i = 0; i < sampleCount; ++i) {
		float value = _workingBuffer[i];

		// Quantize
		float quantized = quantizeValue(std::clamp(value, 0.0f, maxValue), maxValue);

		// Calculate error
		float error = value - quantized;

		// Distribute error using JJN pattern
		//         *   7   5
		// 3   5   7   5   3
		// 1   3   5   3   1
		if (i + 1 < sampleCount) _workingBuffer[i + 1] += error * w1;
		if (i + 2 < sampleCount) _workingBuffer[i + 2] += error * w2;
		if (i + 3 < sampleCount) _workingBuffer[i + 3] += error * w3;
		if (i + 4 < sampleCount) _workingBuffer[i + 4] += error * w4;

		// Convert back to [-1, 1] and store
		samples[i] = (quantized / maxValue) * 2.0f - 1.0f;
	}
}

float DitherEffect::quantizeValue(float value, float maxValue) const {
	return std::round(value);
}

float DitherEffect::generateTPDFDither() {
	// Generate triangular dither (sum of two uniform distributions)
	return (_dist(_rng) + _dist(_rng) - 1.0f);
}

} // namespace rp
