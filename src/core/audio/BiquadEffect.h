#pragma once

#include <cmath>
#include "core/audio/Effect.h"

namespace rp {
	enum class FilterType3 {
		LowPass,
		HighPass,
		BandPass,
		BandStop,
		Peak,
		LowShelf,
		HighShelf,
		AllPass
	};

	class BiquadEffect final : public Effect {
	private:
		FilterType3 _filterType = FilterType3::LowPass;
		f32 _frequency = 1000.0f;  // Hz
		f32 _q = 0.707f;           // Quality factor (1/sqrt(2) for Butterworth response)
		f32 _gain = 0.0f;          // dB (for peak/shelf filters)
		f32 _sampleRate = 44100.0f;

		// Biquad coefficients
		f32 _b0 = 1.0f, _b1 = 0.0f, _b2 = 0.0f;  // Numerator coefficients
		f32 _a1 = 0.0f, _a2 = 0.0f;              // Denominator coefficients (a0 normalized to 1)

		// Filter state (delay lines)
		f32 _x1 = 0.0f, _x2 = 0.0f;  // Input delay line
		f32 _y1 = 0.0f, _y2 = 0.0f;  // Output delay line

	public:
		BiquadEffect() = default;
		~BiquadEffect() = default;

		void process(fw::AudioBuffer& buffer) override;

		// Parameter setters
		void setFilterType(FilterType3 type) { _filterType = type; updateCoefficients(); }
		void setFrequency(f32 frequency) { _frequency = frequency; updateCoefficients(); }
		void setQ(f32 q) { _q = q; updateCoefficients(); }
		void setGain(f32 gain) { _gain = gain; updateCoefficients(); }
		void setSampleRate(f32 sampleRate) { _sampleRate = sampleRate; updateCoefficients(); }

		// Parameter getters
		FilterType3 getFilterType() const { return _filterType; }
		f32 getFrequency() const { return _frequency; }
		f32 getQ() const { return _q; }
		f32 getGain() const { return _gain; }
		f64 getSampleRate() const { return _sampleRate; }

		// Reset filter state
		void reset();

		// Convenience functions for common filter configurations
		void configureLowPass(f32 frequency, f32 q = 0.707f);
		void configureHighPass(f32 frequency, f32 q = 0.707f);
		void configureBandPass(f32 frequency, f32 q = 1.0f);
		void configureBandStop(f32 frequency, f32 q = 1.0f);
		void configurePeaking(f32 frequency, f32 q = 1.0f, f32 gainDb = 6.0f);
		void configureLowShelf(f32 frequency, f32 q = 0.707f, f32 gainDb = 6.0f);
		void configureHighShelf(f32 frequency, f32 q = 0.707f, f32 gainDb = 6.0f);

		// Utility functions
		bool isStable() const;  // Check if filter coefficients are stable
		f32 getMagnitudeResponse(f32 frequency) const;  // Get magnitude response at given frequency

	private:
		// Update filter coefficients based on current parameters
		void updateCoefficients();

		// Helper functions for coefficient calculation
		void calculateLowPassCoefficients();
		void calculateHighPassCoefficients();
		void calculateBandPassCoefficients();
		void calculateBandStopCoefficients();
		void calculatePeakCoefficients();
		void calculateLowShelfCoefficients();
		void calculateHighShelfCoefficients();
		void calculateAllPassCoefficients();
	};
}
