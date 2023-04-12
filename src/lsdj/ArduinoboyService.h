#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class ArduinoboyService final : public TypedSystemService<ArduinoboyServiceSettings> {
	private:
		int32 _lastRow = -1;
		TimeInfo _timeInfo;
		bool _arduinoboyPlaying = false;
		uint8 _keyboardOctave = 0;

	public:
		ArduinoboyService() : TypedSystemService(ARDUINOBOY_SERVICE_TYPE) {}
		~ArduinoboyService() = default;

		void onAfterLoad(System& system) override;

		void onTransportChange(System& system, bool running) override;

		void onMidi(System& system, const fw::MidiMessage& message) override;

		void onMidiClock(System& system);

	private:
		void processInstanceMidiMessage(System& system, const fw::MidiMessage& msg, int channel);

		void processSync(System& system, int sampleCount, int tempoDivisor, char value);
	};
}
