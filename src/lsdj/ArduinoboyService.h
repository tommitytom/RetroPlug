#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class ArduinoboyService final : public SystemService {
	private:
		int32 _lastRow = -1;
		TimeInfo _timeInfo;
		bool _arduinoboyPlaying = false;
		ArduinoboyServiceSettings _settings;

	public:
		ArduinoboyService() : SystemService(ARDUINOBOY_SERVICE_TYPE) {}
		~ArduinoboyService() = default;

		void onMidi(System& system, const fw::MidiMessage& message) override;

		void onMidiClock(System& system);

		void setState(const entt::any& data) override {
			_settings = entt::any_cast<const ArduinoboyServiceSettings&>(data);
		}

		const entt::any getState() const override { 
			return entt::forward_as_any(_settings); 
		}

	private:
		void processInstanceMidiMessage(System& system, const fw::MidiMessage& msg, int channel);

		void processSync(System& system, int sampleCount, int tempoDivisor, char value);
	};
}
