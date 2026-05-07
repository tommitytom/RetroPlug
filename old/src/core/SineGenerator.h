#pragma once

#include "Components.h"
#include "AudioEffect.h"

namespace rp {
	struct SineComponent {
		f32 frequency = 440.0f;
		f32 amplitude = 0.1f;
	};

	struct SineStateComponent {
		f32 phase = 0.0f;
	};

	class SineGenerator : public AudioGenerator<SineComponent, SineStateComponent> {
	public:
		void process(orb::AudioBuffer& out, const SineComponent& comp, SineStateComponent& state) override {
			const uint32 sampleCount = out.getSampleCount();
			const f32 sampleRate = out.getSampleRate();
			f32* outputL = out.getWritePointer(0);
			f32* outputR = out.getWritePointer(1);

			for (uint32 i = 0; i < sampleCount; ++i) {
				const f32 sample = sinf(state.phase) * 0.1f;
				outputL[i] = sample;
				outputR[i] = sample;

				state.phase += (comp.frequency * (2.0f * 3.14159265359f)) / sampleRate;
				if (state.phase > (2.0f * 3.14159265359f)) {
					state.phase -= (2.0f * 3.14159265359f);
				}
			}
		}
	};
	/*
	struct GainComponent {
		f32 gain = 1.0f;
	};
	struct EmptyTag { int value; };
	class GainEffect : public AudioEffect<GainComponent, EmptyTag> {
	public:
		void process(orb::AudioBuffer& out, const GainComponent& comp, EmptyTag&) override {
			out.applyGain(comp.gain);
		}
	};


	enum class FilterType {
		LowPass,
		HighPass,
		BandPass,
		BandStop,
		Peak,
		LowShelf,
		HighShelf,
		AllPass
	};

	struct BiquadComponent {
		FilterType filterType = FilterType::LowPass;
		f32 frequency = 1000.0f;  // Hz
		f32 q = 0.707f;           // Quality factor (1/sqrt(2) for Butterworth response)
		f32 gain = 0.0f;          // dB (for peak/shelf filters)
	};

	struct BiquadStateComponent {
		struct ChannelState {
			// Biquad coefficients
			f32 b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;  // Numerator coefficients
			f32 a1 = 0.0f, a2 = 0.0f;              // Denominator coefficients (a0 normalized to 1)
			// Filter state (delay lines)
			f32 x1 = 0.0f, x2 = 0.0f;  // Input delay line
			f32 y1 = 0.0f, y2 = 0.0f;  // Output delay line
		};

		f32 sampleRate = 48000.0f;
		std::vector<ChannelState> coeffs;
	};

	class BiquadEffect : public AudioEffect<BiquadComponent, BiquadStateComponent> {
	public:
		void process(orb::AudioBuffer& buffer, const BiquadComponent& comp, BiquadStateComponent& state) override {
			using SampleType = orb::AudioBuffer::SampleTypeT;
			const uint32 sampleCount = buffer.getSampleCount();
			const uint32 channelCount = buffer.getChannelCount();
			const f32 sampleRate = buffer.getSampleRate();

			if (channelCount != (uint32)state.coeffs.size()) {
				state.coeffs.resize(channelCount);
			}

			if (sampleRate != state.sampleRate) {
				// regen
			}

			for (uint32 channelIdx = 0; channelIdx < channelCount; ++channelIdx) {
				SampleType* channelData = buffer.getWritePointer(channelIdx);
				BiquadStateComponent::ChannelState& st = state.coeffs[channelIdx];

				for (uint32 i = 0; i < sampleCount; ++i) {
					SampleType input = channelData[i];

					// Apply biquad filter equation: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
					SampleType output = st.b0 * input + st.b1 * st.x1 + st.b2 * st.x2 - st.a1 * st.y1 - st.a2 * st.y2;

					// Update delay lines
					st.x2 = st.x1;
					st.x1 = input;
					st.y2 = st.y1;
					st.y1 = output;

					// Clamp output to prevent numerical issues
					output = std::clamp(output, -1.0f, 1.0f);

					// Set output sample
					channelData[i] = output;
				}
			}
		}
	};*/
}
