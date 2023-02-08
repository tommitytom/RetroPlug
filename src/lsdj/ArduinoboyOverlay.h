#pragma once

#include "ui/Menu.h"
#include "ui/SystemOverlay.h"
#include "lsdj/ArduinoboyService.h"

namespace rp {
	class ArduinoboyOverlay final : public TypedSystemOverlay<ArduinoboyServiceSettings> {
	public:
		ArduinoboyOverlay() {
			setType<ArduinoboyOverlay>();
		}

		void onMenu(fw::Menu& menu) override {
			const ArduinoboyServiceSettings& settings = getServiceState();

			menu.subMenu("LSDJ")
				.multiSelect("Arduinoboy Sync", { "Off", "MIDI Sync [MIDI]", "MIDI Sync Arduinoboy [MIDI]", "MIDI Map [MI. MAP]" }, (int)settings.syncMode, [&](int mode) {
					setField<&ArduinoboyServiceSettings::syncMode>((LsdjSyncMode)mode);
				})
				.parent();
		}
	};
}
