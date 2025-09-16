#include "Effects.h"

#include "foundation/Constants.h"

namespace rp {
	struct FilterState {
		f32 sampleRate = 48000.0f;  // Sample rate in Hz

		// Biquad coefficients
		f32 b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;  // Numerator coefficients
		f32 a1 = 0.0f, a2 = 0.0f;              // Denominator coefficients (a0 normalized to 1)

		// Filter state (delay lines)
		f32 x1 = 0.0f, x2 = 0.0f;  // Input delay line
		f32 y1 = 0.0f, y2 = 0.0f;  // Output delay line
	};

	void calculateLowPassCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha;
		state.b0 = (1.0f - cos_omega) / (2.0f * a0);
		state.b1 = (1.0f - cos_omega) / a0;
		state.b2 = (1.0f - cos_omega) / (2.0f * a0);
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha) / a0;
	}

	void calculateHighPassCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha;
		state.b0 = (1.0f + cos_omega) / (2.0f * a0);
		state.b1 = -(1.0f + cos_omega) / a0;
		state.b2 = (1.0f + cos_omega) / (2.0f * a0);
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha) / a0;
	}

	void calculateBandPassCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha;
		state.b0 = sin_omega / (2.0f * a0);
		state.b1 = 0.0f;
		state.b2 = -sin_omega / (2.0f * a0);
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha) / a0;
	}

	void calculateBandStopCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha;
		state.b0 = 1.0f / a0;
		state.b1 = (-2.0f * cos_omega) / a0;
		state.b2 = 1.0f / a0;
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha) / a0;
	}

	void calculatePeakCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 A = std::pow(10.0f, effect.gain / 40.0f);  // Convert dB to linear scale
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha / A;
		state.b0 = (1.0f + alpha * A) / a0;
		state.b1 = (-2.0f * cos_omega) / a0;
		state.b2 = (1.0f - alpha * A) / a0;
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha / A) / a0;
	}

	void calculateLowShelfCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 A = std::pow(10.0f, effect.gain / 40.0f);  // Convert dB to linear scale
		f32 S = 1.0f;  // Shelf slope parameter
		f32 beta = std::sqrt(A) / effect.q;

		f32 a0 = (A + 1.0f) + (A - 1.0f) * cos_omega + beta * sin_omega;
		state.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cos_omega + beta * sin_omega)) / a0;
		state.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_omega)) / a0;
		state.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cos_omega - beta * sin_omega)) / a0;
		state.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cos_omega)) / a0;
		state.a2 = ((A + 1.0f) + (A - 1.0f) * cos_omega - beta * sin_omega) / a0;
	}

	void calculateHighShelfCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 A = std::pow(10.0f, effect.gain / 40.0f);  // Convert dB to linear scale
		f32 S = 1.0f;  // Shelf slope parameter
		f32 beta = std::sqrt(A) / effect.q;

		f32 a0 = (A + 1.0f) - (A - 1.0f) * cos_omega + beta * sin_omega;
		state.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_omega + beta * sin_omega)) / a0;
		state.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_omega)) / a0;
		state.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_omega - beta * sin_omega)) / a0;
		state.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_omega)) / a0;
		state.a2 = ((A + 1.0f) - (A - 1.0f) * cos_omega - beta * sin_omega) / a0;
	}

	void calculateAllPassCoefficients(const FilterEffect& effect, FilterState& state) {
		f32 omega = 2.0f * fw::PI * effect.frequency / state.sampleRate;
		f32 sin_omega = std::sin(omega);
		f32 cos_omega = std::cos(omega);
		f32 alpha = sin_omega / (2.0f * effect.q);

		f32 a0 = 1.0f + alpha;
		state.b0 = (1.0f - alpha) / a0;
		state.b1 = (-2.0f * cos_omega) / a0;
		state.b2 = (1.0f + alpha) / a0;
		state.a1 = (-2.0f * cos_omega) / a0;
		state.a2 = (1.0f - alpha) / a0;
	}

	void updateCoefficients(const FilterEffect& effect, FilterState& state) {
		switch (effect.filterType) {
		case FilterType::LowPass:
			calculateLowPassCoefficients(effect, state);
			break;
		case FilterType::HighPass:
			calculateHighPassCoefficients(effect, state);
			break;
		case FilterType::BandPass:
			calculateBandPassCoefficients(effect, state);
			break;
		case FilterType::BandStop:
			calculateBandStopCoefficients(effect, state);
			break;
		case FilterType::Peak:
			calculatePeakCoefficients(effect, state);
			break;
		case FilterType::LowShelf:
			calculateLowShelfCoefficients(effect, state);
			break;
		case FilterType::HighShelf:
			calculateHighShelfCoefficients(effect, state);
			break;
		case FilterType::AllPass:
			calculateAllPassCoefficients(effect, state);
			break;
		}
	}

	void processEffect(const FilterEffect& effect, fw::Float32Buffer& target, f32 sampleRate) {
		FilterState state;

		updateCoefficients(effect, state);

		const size_t sampleCount = target.size();
		f32* channelData = target.data();

		for (size_t i = 0; i < sampleCount; ++i) {
			auto input = channelData[i];

			// Apply biquad filter equation: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
			f32 output = state.b0 * input + state.b1 * state.x1 + state.b2 * state.x2 - state.a1 * state.y1 - state.a2 * state.y2;

			// Update delay lines
			state.x2 = state.x1;
			state.x1 = input;
			state.y2 = state.y1;
			state.y1 = output;

			// Clamp output to prevent numerical issues
			output = std::clamp(output, -1.0f, 1.0f);

			// Set output sample
			channelData[i] = output;
		}
	}
}
