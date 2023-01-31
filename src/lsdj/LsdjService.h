#pragma once

#include "core/SystemService.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/Ram.h"

namespace rp {
	class LsdjService final : public SystemService {
	private:
		int32 _lastRow = -1;
		TimeInfo _timeInfo;
		bool _arduinoboyPlaying = false;
		lsdj::MemoryOffsets _ramOffsets;
		bool _offsetsValid = false;
		bool _romValid = false;
		uint64 _songHash = 0;
		//LsdjRefresher _refresher;

		LsdjServiceSettings _settings;

	public:
		LsdjService() : SystemService(0x15D115D1) {}
		~LsdjService() = default;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;

		void onMidi(System& system, const fw::MidiMessage& message) override;

		void onMidiClock(System& system);

		void setState(const entt::any& data) override {
			_settings = entt::any_cast<const LsdjServiceSettings&>(data);
		}

		const entt::any getState() const override { 
			return entt::forward_as_any(_settings); 
		}

	private:
		void processInstanceMidiMessage(System& system, const fw::MidiMessage& msg, int channel);

		void processSync(System& system, int sampleCount, int tempoDivisor, char value);
	};
}
