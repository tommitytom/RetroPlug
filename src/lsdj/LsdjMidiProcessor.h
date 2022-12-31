#pragma once

#include "core/MidiProcessor.h"

namespace rp {
	enum class LsdjSyncMode {
		Off,
		MidiSync,
		MidiSyncArduinoboy,
		MidiMap
	};

	struct TimeInfo {
		f64 sampleRate = 44100.0;
		f64 tempo = 120.0;
		f64 ppqPos = 0;
	};

	class LsdjMidiProcesor final : public MidiProcessor {
	private:
		LsdjSyncMode _syncMode = LsdjSyncMode::Off;
		bool _arduinoboyPlaying = false;
		uint32 _tempoDivisor = 1;
		int32 _lastRow = -1;

		TimeInfo _timeInfo;

	public:
		void onMidi(SystemIo& io, const fw::MidiMessage& message) override;

		void onMidiClock();

	private:
		void processInstanceMidiMessage(SystemIo& io, const fw::MidiMessage& msg, int channel);

		void processSync(SystemIo& io, int sampleCount, int tempoDivisor, char value);
	};
}
