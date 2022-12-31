#pragma once

#include "foundation/Types.h"
#include "audio/MidiMessage.h"

namespace fw {
	class AudioProcessor {
	public:
		virtual void onRender(f32* output, const f32* input, uint32 frameCount) = 0;

		virtual void onMidi(const fw::MidiMessage& message) {}
	};
}
