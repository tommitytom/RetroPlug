#pragma once

#include "core/audio/Effect.h"
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>

namespace rp {
	enum class DitherMode {
		ErrorDiffusion,      // Floyd-Steinberg style error diffusion
		SierraLite,          // Sierra Lite error diffusion (lighter weight)
		HighPassTPDF,        // High-Pass TPDF (Triangular Probability Density Function)
		ShapedTPDF2ndOrder,  // Shaped TPDF with 2nd order noise shaping
		JJNErrorDiffusion    // Jarvis-Judice-Ninke error diffusion
	};

	class DitherEffect final : public Effect {
	private:
		DitherMode _mode = DitherMode::ErrorDiffusion;
		int _bitDepth = 4;
		bool _enabled = true;

		// Random number generation
		std::mt19937 _rng;
		std::uniform_real_distribution<float> _dist;

		// State variables for different dithering modes
		// Error diffusion state
		float _errorDiffusionError = 0.0f;

		// High-pass TPDF state
		float _prevDither = 0.0f;
		float _shapingState = 0.0f;

		// 2nd order shaped TPDF state
		float _error1 = 0.0f;
		float _error2 = 0.0f;

		// Working buffers for multi-sample error diffusion
		std::vector<float> _workingBuffer;

	public:
		DitherEffect();
		~DitherEffect() = default;

		void process(fw::AudioBuffer& buffer) override;

		// Parameter setters
		void setMode(DitherMode mode) { _mode = mode; reset(); }
		void setBitDepth(int bitDepth) { _bitDepth = std::clamp(bitDepth, 1, 16); }
		void setEnabled(bool enabled) { _enabled = enabled; }

		// Parameter getters
		DitherMode getMode() const { return _mode; }
		int getBitDepth() const { return _bitDepth; }
		bool isEnabled() const { return _enabled; }

		// Reset all state variables
		void reset();

		// Convenience functions for common configurations
		void configureErrorDiffusion(int bitDepth = 4);
		void configureSierraLite(int bitDepth = 4);
		void configureHighPassTPDF(int bitDepth = 4);
		void configureShapedTPDF2ndOrder(int bitDepth = 4);
		void configureJJNErrorDiffusion(int bitDepth = 4);

	private:
		// Individual dithering algorithm implementations
		void processErrorDiffusion(float* samples, uint32 sampleCount);
		void processSierraLite(float* samples, uint32 sampleCount);
		void processHighPassTPDF(float* samples, uint32 sampleCount);
		void processShapedTPDF2ndOrder(float* samples, uint32 sampleCount);
		void processJJNErrorDiffusion(float* samples, uint32 sampleCount);

		// Helper functions
		float quantizeValue(float value, float maxValue) const;
		float generateTPDFDither();
	};
}
