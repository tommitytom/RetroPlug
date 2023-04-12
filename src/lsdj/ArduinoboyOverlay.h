#pragma once

#include "ui/Menu.h"
#include "ui/SystemOverlay.h"
#include "lsdj/ArduinoboyService.h"

namespace rp {
	class ArduinoboyOverlay final : public TypedSystemOverlay<ArduinoboyServiceSettings> {
	private:
		bool _keyCapture = false;
		
	public:
		ArduinoboyOverlay() {
			setType<ArduinoboyOverlay>();
			setFocusPolicy(fw::FocusPolicy::Click);
		}

		bool onKey(const fw::KeyEvent& ev) override {			
			if (ev.key == VirtualKey::P && ev.down) {
				_keyCapture = !_keyCapture;
				return true;
			}
			
			if (!_keyCapture) {
				return false;
			}

			if (ev.key >= VirtualKey::A && ev.key <= VirtualKey::Z) {
				getNode()->sendEvent(getState<fw::EventNode>(), fw::KeyEvent(ev));
				return true;
			}

			return false;
		}

		void onMenu(fw::Menu& menu) override {
			const ArduinoboyServiceSettings& settings = getServiceState();
			
			menu.subMenu("LSDJ")
				.multiSelect("Arduinoboy Sync", { "Off", "MIDI Sync [MIDI]", "MIDI Sync Arduinoboy [MIDI]", "MIDI Map [MI. MAP]", "Keyboard [KEYBD]", "Keyboard MIDI [KEYBD]" }, (int)settings.syncMode, [&](int mode) {
					setField<&ArduinoboyServiceSettings::syncMode>((LsdjSyncMode)mode);
				})
				.select("Autoplay", settings.autoPlay, [&](bool autoplay) {
					setField<&ArduinoboyServiceSettings::autoPlay>(autoplay);
				})
				.parent();
		}
	};
}
