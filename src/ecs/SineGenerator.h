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
		void process(fw::AudioBuffer& out, const SineComponent& comp, SineStateComponent& state) override {
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
}
