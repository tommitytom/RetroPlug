#include "BiquadEffect.h"
#include <cmath>
#include <algorithm>

constexpr f32 PI = 3.14159265358979323846f;

namespace rp {

void BiquadEffect::process(fw::AudioBuffer& buffer) {
	assert(buffer.getChannelCount() == 1);

	// Check if buffer's sample rate differs from our current sample rate
	f32 bufferSampleRate = (f32)buffer.getSampleRate();
	if (bufferSampleRate != _sampleRate && bufferSampleRate > 0.0f) {
		setSampleRate(bufferSampleRate);
	}

	const uint32 sampleCount = buffer.getSampleCount();
	const uint32 channelCount = buffer.getChannelCount();

	for (uint32 channelIdx = 0; channelIdx < channelCount; ++channelIdx) {
		auto* channelData = buffer.getWritePointer(channelIdx);

		for (uint32 i = 0; i < sampleCount; ++i) {
			auto input = channelData[i];

			// Apply biquad filter equation: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
			f32 output = _b0 * input + _b1 * _x1 + _b2 * _x2 - _a1 * _y1 - _a2 * _y2;

			// Update delay lines
			_x2 = _x1;
			_x1 = input;
			_y2 = _y1;
			_y1 = output;

			// Clamp output to prevent numerical issues
			output = std::clamp(output, -1.0f, 1.0f);

			// Set output sample
			channelData[i] = output;
		}
	}
}

void BiquadEffect::reset() {
	_x1 = _x2 = 0.0f;
	_y1 = _y2 = 0.0f;
}

void BiquadEffect::updateCoefficients() {
	switch (_filterType) {
		case FilterType::LowPass:
			calculateLowPassCoefficients();
			break;
		case FilterType::HighPass:
			calculateHighPassCoefficients();
			break;
		case FilterType::BandPass:
			calculateBandPassCoefficients();
			break;
		case FilterType::BandStop:
			calculateBandStopCoefficients();
			break;
		case FilterType::Peak:
			calculatePeakCoefficients();
			break;
		case FilterType::LowShelf:
			calculateLowShelfCoefficients();
			break;
		case FilterType::HighShelf:
			calculateHighShelfCoefficients();
			break;
		case FilterType::AllPass:
			calculateAllPassCoefficients();
			break;
	}
}

void BiquadEffect::calculateLowPassCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha;
	_b0 = (1.0f - cos_omega) / (2.0f * a0);
	_b1 = (1.0f - cos_omega) / a0;
	_b2 = (1.0f - cos_omega) / (2.0f * a0);
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha) / a0;
}

void BiquadEffect::calculateHighPassCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha;
	_b0 = (1.0f + cos_omega) / (2.0f * a0);
	_b1 = -(1.0f + cos_omega) / a0;
	_b2 = (1.0f + cos_omega) / (2.0f * a0);
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha) / a0;
}

void BiquadEffect::calculateBandPassCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha;
	_b0 = sin_omega / (2.0f * a0);
	_b1 = 0.0f;
	_b2 = -sin_omega / (2.0f * a0);
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha) / a0;
}

void BiquadEffect::calculateBandStopCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha;
	_b0 = 1.0f / a0;
	_b1 = (-2.0f * cos_omega) / a0;
	_b2 = 1.0f / a0;
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha) / a0;
}

void BiquadEffect::calculatePeakCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 A = std::pow(10.0f, _gain / 40.0f);  // Convert dB to linear scale
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha / A;
	_b0 = (1.0f + alpha * A) / a0;
	_b1 = (-2.0f * cos_omega) / a0;
	_b2 = (1.0f - alpha * A) / a0;
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha / A) / a0;
}

void BiquadEffect::calculateLowShelfCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 A = std::pow(10.0f, _gain / 40.0f);  // Convert dB to linear scale
	f32 S = 1.0f;  // Shelf slope parameter
	f32 beta = std::sqrt(A) / _q;

	f32 a0 = (A + 1.0f) + (A - 1.0f) * cos_omega + beta * sin_omega;
	_b0 = (A * ((A + 1.0f) - (A - 1.0f) * cos_omega + beta * sin_omega)) / a0;
	_b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_omega)) / a0;
	_b2 = (A * ((A + 1.0f) - (A - 1.0f) * cos_omega - beta * sin_omega)) / a0;
	_a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cos_omega)) / a0;
	_a2 = ((A + 1.0f) + (A - 1.0f) * cos_omega - beta * sin_omega) / a0;
}

void BiquadEffect::calculateHighShelfCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 A = std::pow(10.0f, _gain / 40.0f);  // Convert dB to linear scale
	f32 S = 1.0f;  // Shelf slope parameter
	f32 beta = std::sqrt(A) / _q;

	f32 a0 = (A + 1.0f) - (A - 1.0f) * cos_omega + beta * sin_omega;
	_b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_omega + beta * sin_omega)) / a0;
	_b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_omega)) / a0;
	_b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_omega - beta * sin_omega)) / a0;
	_a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_omega)) / a0;
	_a2 = ((A + 1.0f) - (A - 1.0f) * cos_omega - beta * sin_omega) / a0;
}

// Convenience functions for common filter configurations
void BiquadEffect::configureLowPass(f32 frequency, f32 q) {
	setFilterType(FilterType::LowPass);
	setFrequency(frequency);
	setQ(q);
}

void BiquadEffect::configureHighPass(f32 frequency, f32 q) {
	setFilterType(FilterType::HighPass);
	setFrequency(frequency);
	setQ(q);
}

void BiquadEffect::configureBandPass(f32 frequency, f32 q) {
	setFilterType(FilterType::BandPass);
	setFrequency(frequency);
	setQ(q);
}

void BiquadEffect::configureBandStop(f32 frequency, f32 q) {
	setFilterType(FilterType::BandStop);
	setFrequency(frequency);
	setQ(q);
}

void BiquadEffect::configurePeaking(f32 frequency, f32 q, f32 gainDb) {
	setFilterType(FilterType::Peak);
	setFrequency(frequency);
	setQ(q);
	setGain(gainDb);
}

void BiquadEffect::configureLowShelf(f32 frequency, f32 q, f32 gainDb) {
	setFilterType(FilterType::LowShelf);
	setFrequency(frequency);
	setQ(q);
	setGain(gainDb);
}

void BiquadEffect::configureHighShelf(f32 frequency, f32 q, f32 gainDb) {
	setFilterType(FilterType::HighShelf);
	setFrequency(frequency);
	setQ(q);
	setGain(gainDb);
}

// Utility functions
bool BiquadEffect::isStable() const {
	// A biquad filter is stable if the poles are inside the unit circle
	// This is equivalent to checking if |a2| < 1 and |a1| < 1 + a2
	return (std::abs(_a2) < 1.0f) && (std::abs(_a1) < (1.0f + _a2));
}

f32 BiquadEffect::getMagnitudeResponse(f32 frequency) const {
	if (_sampleRate <= 0.0f) return 1.0f;

	f32 omega = 2.0f * PI * frequency / _sampleRate;
	f32 cos_omega = std::cos(omega);
	f32 sin_omega = std::sin(omega);
	f32 cos_2omega = std::cos(2.0f * omega);

	// Calculate numerator magnitude squared
	f32 num_mag_sq = _b0 * _b0 + _b1 * _b1 + _b2 * _b2 +
		2.0f * _b0 * _b1 * cos_omega +
		2.0f * _b0 * _b2 * cos_2omega +
		2.0f * _b1 * _b2 * cos_omega;

	// Calculate denominator magnitude squared
	f32 den_mag_sq = 1.0f + _a1 * _a1 + _a2 * _a2 +
		2.0f * _a1 * cos_omega +
		2.0f * _a2 * cos_2omega +
		2.0f * _a1 * _a2 * cos_omega;

	// Return magnitude response
	return std::sqrt(num_mag_sq / den_mag_sq);
}

void BiquadEffect::calculateAllPassCoefficients() {
	f32 omega = 2.0f * PI * _frequency / _sampleRate;
	f32 sin_omega = std::sin(omega);
	f32 cos_omega = std::cos(omega);
	f32 alpha = sin_omega / (2.0f * _q);

	f32 a0 = 1.0f + alpha;
	_b0 = (1.0f - alpha) / a0;
	_b1 = (-2.0f * cos_omega) / a0;
	_b2 = (1.0f + alpha) / a0;
	_a1 = (-2.0f * cos_omega) / a0;
	_a2 = (1.0f - alpha) / a0;
}

}  // namespace rp
