#pragma once

#include "ui/Menu.h"
#include "ui/SystemOverlay.h"
#include "lsdj/ArduinoboyService.h"

namespace rp {
	class ArduinoboyOverlay final : public SystemOverlay {
	private:
		ArduinoboyServiceSettings _settings;
		
	public:
		ArduinoboyOverlay() {
			setType<ArduinoboyOverlay>();
		}

		void onMenu(fw::Menu& menu) override {
			menu.subMenu("LSDJ")
				.multiSelect("Arduinoboy Sync", { "Off", "MIDI Sync [MIDI]", "MIDI Sync Arduinoboy [MIDI]", "MIDI Map [MI. MAP]"}, &_settings.syncMode)
				.parent();
		}
	};
}
