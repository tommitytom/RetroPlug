#pragma once

#include "core/SystemService.h"
#include "audio/TimeInfo.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class ArduinoboyService final : public TypedSystemService<ArduinoboyServiceSettings> {
	private:
		int32 _lastRow = -1;
		bool _arduinoboyPlaying = false;
		uint8 _keyboardOctave = 0;

	public:
		ArduinoboyService() : TypedSystemService(ARDUINOBOY_SERVICE_TYPE) {}
		~ArduinoboyService() = default;

		void onAfterLoad(System& system) override;

		void onTransportChange(System& system, bool running) override;

		void onTransportUpdate(System& system, const fw::TimeInfo& timeInfo) override;

		void onMidi(System& system, const fw::MidiMessage& message) override;

		void onMidiClock(System& system);

	private:
		void processSync(System& system, const fw::TimeInfo& timeInfo, int32 tempoDivisor, uint8 value);
	};
}
