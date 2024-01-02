#pragma once

#include <refl.hpp>
#include "foundation/Attributes.h"
#include "foundation/node/NodeState.h"
#include "audio/AudioBuffer.h"

namespace fw {
	struct NodeBase {};
	struct AudioNode : NodeBase {};
}

namespace fw {
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
}

REFL_AUTO(
	type(fw::SineOsc::Input),
	field(freq, fw::reflutil::TypedAttribute<f32>(0.0f, 20000.0f)),
	field(amp, fw::reflutil::RangeAttribute(0.0f, 1.0f))
)

REFL_AUTO(
	type(fw::SineOsc::Output),
	field(output)
)

REFL_AUTO(
	type(fw::SineOsc)
)


namespace fw {
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
	type(fw::SineLfo::Input),
	field(freq, fw::reflutil::RangeAttribute(0.0f, 20000.0f)),
	field(amp, fw::reflutil::RangeAttribute(0.0f, 1.0f))
)

REFL_AUTO(
	type(fw::SineLfo::Output),
	field(output)
)

REFL_AUTO(
	type(fw::SineLfo)
)


namespace fw {
	struct AddFloatNode {
		struct Input {
			f32 in1 = 0;
			f32 in2 = 0;
		};

		struct Output {
			f32 output;
		};
	};
}

REFL_AUTO(
	type(fw::AddFloatNode::Input),
	field(in1),
	field(in2)
)

REFL_AUTO(
	type(fw::AddFloatNode::Output),
	field(output)
)

REFL_AUTO(
	type(fw::AddFloatNode)
)

namespace fw {
	struct ConstFloatNode {
		struct Input {};

		struct Output {
			f32 value;
		};
	};
}

REFL_AUTO(
	type(fw::ConstFloatNode::Input)
)

REFL_AUTO(
	type(fw::ConstFloatNode::Output),
	field(value)
)

REFL_AUTO(
	type(fw::ConstFloatNode)
)















namespace fw {
	void processSine(NodeState<SineOsc>& node);
	void processSineLfo(NodeState<SineLfo>& node);
	void processAddFloat(NodeState<AddFloatNode>& node);
	void processConstFloatNode(NodeState<ConstFloatNode>& node);
}
