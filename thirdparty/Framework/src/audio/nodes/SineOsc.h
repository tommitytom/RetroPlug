#pragma once

#include <refl.hpp>
#include "foundation/Attributes.h"
#include "foundation/node/NodeState.h"
#include "audio/AudioBuffer.h"

namespace fw {
	struct NodeBase {};
	struct AudioNode : NodeBase {};

	struct SineOsc : AudioNode {
		struct Input {
			f32 freq = 240.0f;
			f32 amp = 0.5f;
		};

		struct Output {
			MonoAudioBuffer output;
		};

		f32 phase = 0.0f;
	};

	struct SineLfo {
		struct Input {
			f32 freq = 240.0f;
			f32 amp = 0.5f;
		};

		struct Output {
			f32 output;
		};

		f32 phase = 0.0f;
	};
}

REFL_AUTO(
	type(fw::SineOsc::Input),
	field(freq, fw::RangeAttribute(0.0f, 20000.0f)),
	field(amp, fw::RangeAttribute(0.0f, 1.0f))
)

REFL_AUTO(
	type(fw::SineOsc::Output),
	field(output)
)

REFL_AUTO(
	type(fw::SineOsc)
)


REFL_AUTO(
	type(fw::SineLfo::Input),
	field(freq, fw::RangeAttribute(0.0f, 20000.0f)),
	field(amp, fw::RangeAttribute(0.0f, 1.0f))
)

REFL_AUTO(
	type(fw::SineLfo::Output),
	field(output)
)

REFL_AUTO(
	type(fw::SineLfo)
)

namespace fw {
	void processSine(NodeState<SineOsc>& node);
	void processSineLfo(NodeState<SineLfo>& node);
}
