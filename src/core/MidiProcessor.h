#pragma once

#include <memory>
#include <vector>

#include "audio/MidiMessage.h"

#include "core/System.h"

namespace rp {
	class MidiProcessor {
	public:
		virtual void onMidi(SystemIo& io, const fw::MidiMessage& message) = 0;
	};

	using MidiProcessorPtr = std::shared_ptr<MidiProcessor>;
}
