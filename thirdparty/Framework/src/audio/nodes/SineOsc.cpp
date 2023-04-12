#include "SineOsc.h"

namespace fw {
	void processSine(NodeState<SineOsc>& node) {
		SineOsc& state = node.get();
		const NodeState<SineOsc>::Input& input = node.input();
		SineOsc::Output& output = node.output();

		f32 amp = input.amp();
		f32 freq = input.freq();

		for (uint32 i = 0; i < node.frameCount; ++i) {
			f32 sample = sinf(state.phase) * input.amp();

			state.phase += 2.0f * PI * input.freq() / 48000.0f;
			if (state.phase > 2.0f * PI) {
				state.phase -= 2.0f * PI;
			}

			output.output.setSample(i, 0, sample);
		}
	}

	void processSineLfo(NodeState<SineLfo>& node) {
		SineLfo& state = node.get();
		const NodeState<SineLfo>::Input& input = node.input();
		SineLfo::Output& output = node.output();

		f32 amp = input.amp();
		f32 freq = input.freq();

		f32 sample = sinf(state.phase) * input.amp();

		state.phase += 2.0f * PI * input.freq() / 400.0f;
		if (state.phase > 2.0f * PI) {
			state.phase -= 2.0f * PI;
		}

		output.output = 240.0f + (sample * 20.0f);
	}
}
