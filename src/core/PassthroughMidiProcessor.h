#pragma once

#include "core/MidiProcessor.h"

namespace rp {
	class PassthroughMidiProcessor final : public MidiProcessor {
	public:
		void onMidi(SystemIo& io, const fw::MidiMessage& message) override {
			io.input.serial.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.status });
			io.input.serial.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.data1 });
			io.input.serial.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.data2 });
		}
	};
}
