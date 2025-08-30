#pragma once

#include "core/SystemService.h"
#include "audio/TimeInfo.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class ArduinoboyService final : public TypedSystemService<ArduinoboyServiceSettings, ARDUINOBOY_SERVICE_TYPE> {
	private:
		int32 _lastRow = -1;
		bool _arduinoboyPlaying = false;
		uint8 _keyboardOctave = 0;

	public:
		ArduinoboyService() = default;
		~ArduinoboyService() = default;

		void onAfterLoad(System& system) override;

		void onTransportChange(System& system, bool running) override;

		void onTransportUpdate(System& system, const fw::TimeInfo& timeInfo) override;

		void onMidi(System& system, const fw::MidiMessage& message) override;

		void onMidiClock(System& system) override;

	private:
		void processSync(System& system, const fw::TimeInfo& timeInfo, int32 tempoDivisor, uint8 value);
	};
}
