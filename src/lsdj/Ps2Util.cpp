#include "Ps2Util.h"

namespace rp {
	int Ps2Util::writeExtended(fw::VirtualKey vk, uint8_t* target) {
		switch (vk) {
		case fw::VirtualKey::LeftWin:
		case fw::VirtualKey::RightCtrl:
		case fw::VirtualKey::RightWin:
		case fw::VirtualKey::Insert:
		case fw::VirtualKey::Home:
		case fw::VirtualKey::Delete:
		case fw::VirtualKey::End:
		case fw::VirtualKey::Divide:
		case fw::VirtualKey::LeftArrow:
		case fw::VirtualKey::RightArrow:
		case fw::VirtualKey::UpArrow:
		case fw::VirtualKey::DownArrow:
		case fw::VirtualKey::PageUp:
		case fw::VirtualKey::PageDown:
		case fw::VirtualKey::Sleep:
		case fw::VirtualKey::PrintScreen:
			target[0] = 0xE0;
			return 1;
		}

		// Unknown:
		// APPS :: make: E0, 2F ------ break: E0, F0, 2F

		// Don't have a VK:
		// R ALT :: make: E0,11 --------- break: E0,F0,11
		// KP EN :: make: E0, 5A --------- break: E0, F0, 5A

		// Print screen and pause are special cases

		return 0;
	}

	int Ps2Util::getMakeCode(fw::VirtualKey vk, uint8_t* target, bool includeExt) {
		int o = 0;
		if (includeExt) {
			o = writeExtended(vk, target);
		}

		target[o] = 0;

		switch (vk) {

		case fw::VirtualKey::Esc: target[o] = 0x76; break;
		case fw::VirtualKey::F1: target[o] = 0x05; break;
		case fw::VirtualKey::F2: target[o] = 0x06; break;
		case fw::VirtualKey::F3: target[o] = 0x04; break;
		case fw::VirtualKey::F4: target[o] = 0x0C; break;
		case fw::VirtualKey::F5: target[o] = 0x03; break;
		case fw::VirtualKey::F6: target[o] = 0x0B; break;
		case fw::VirtualKey::F7: target[o] = 0x83; break;
		case fw::VirtualKey::F8: target[o] = 0x0A; break;
		case fw::VirtualKey::F9: target[o] = 0x01; break;
		case fw::VirtualKey::F10: target[o] = 0x09; break;
		case fw::VirtualKey::F11: target[o] = 0x78; break;
		case fw::VirtualKey::F12: target[o] = 0x07; break;
		case fw::VirtualKey::Space: target[o] = 0x29; break;
		case fw::VirtualKey::Enter: target[o] = 0x5A; break;

		case fw::VirtualKey::Oem1: target[o] = 0x4C; break; // ;
		case fw::VirtualKey::Oem2: target[o] = 0x4A; break; // /
		case fw::VirtualKey::Oem3: target[o] = 0x0E; break; // `
		case fw::VirtualKey::Oem4: target[o] = 0x54; break; // [
		case fw::VirtualKey::Oem5: target[o] = 0x5D; break; // \ (backslash)
		case fw::VirtualKey::Oem6: target[o] = 0x5B; break; // ]
		case fw::VirtualKey::Oem7: target[o] = 0x52; break; // '

		case fw::VirtualKey::OemMinus: target[o] = 0x4E; break;
		case fw::VirtualKey::OemPlus: target[o] = 0x55; break;
		case fw::VirtualKey::OemPeriod: target[o] = 0x49; break;
		case fw::VirtualKey::OemComma: target[o] = 0x41; break;

		case fw::VirtualKey::Subtract: target[o] = 0x7B; break;
		case fw::VirtualKey::Add: target[o] = 0x79; break;
		case fw::VirtualKey::Divide: target[o] = 0x4A; break;
		case fw::VirtualKey::Multiply: target[o] = 0x7C; break;
		case fw::VirtualKey::Decimal: target[o] = 0x71; break;

		case fw::VirtualKey::NumPad0: target[o] = 0x70; break;
		case fw::VirtualKey::NumPad1: target[o] = 0x69; break;
		case fw::VirtualKey::NumPad2: target[o] = 0x72; break;
		case fw::VirtualKey::NumPad3: target[o] = 0x7A; break;
		case fw::VirtualKey::NumPad4: target[o] = 0x6B; break;
		case fw::VirtualKey::NumPad5: target[o] = 0x73; break;
		case fw::VirtualKey::NumPad6: target[o] = 0x74; break;
		case fw::VirtualKey::NumPad7: target[o] = 0x6C; break;
		case fw::VirtualKey::NumPad8: target[o] = 0x75; break;
		case fw::VirtualKey::NumPad9: target[o] = 0x7D; break;

		case fw::VirtualKey::Backspace: target[o] = 0x66; break;
		case fw::VirtualKey::Tab: target[o] = 0x0D; break;
		case fw::VirtualKey::Caps: target[o] = 0x58; break;

		case fw::VirtualKey::Ctrl: target[o] = 0x14; break;
		case fw::VirtualKey::Shift: target[o] = 0x12; break;
		case fw::VirtualKey::Alt: target[o] = 0x11; break;

		case fw::VirtualKey::LeftShift: target[o] = 0x12; break;
		case fw::VirtualKey::LeftCtrl: target[o] = 0x14; break;
		case fw::VirtualKey::LeftWin: target[o] = 0x1F; break;

		case fw::VirtualKey::RightShift: target[o] = 0x59; break;
		case fw::VirtualKey::RightCtrl: target[o] = 0x1F; break;
		case fw::VirtualKey::RightWin: target[o] = 0x1F; break;


		case fw::VirtualKey::Scroll: target[o] = 0x7E; break;
		case fw::VirtualKey::Insert: target[o] = 0x70; break;

		case fw::VirtualKey::Home: target[o] = 0x6C; break;
		case fw::VirtualKey::Delete: target[o] = 0x71; break;
		case fw::VirtualKey::End: target[o] = 0x69; break;

		case fw::VirtualKey::NumLock: target[o] = 0x77; break;

		case fw::VirtualKey::LeftArrow: target[o] = 0x6B; break;
		case fw::VirtualKey::RightArrow: target[o] = 0x74; break;
		case fw::VirtualKey::UpArrow: target[o] = 0x75; break;
		case fw::VirtualKey::DownArrow: target[o] = 0x72; break;
		case fw::VirtualKey::PageUp: target[o] = 0x7D; break;
		case fw::VirtualKey::PageDown: target[o] = 0x7A; break;

		case fw::VirtualKey::Num0: target[o] = 0x45; break;
		case fw::VirtualKey::Num1: target[o] = 0x16; break;
		case fw::VirtualKey::Num2: target[o] = 0x1E; break;
		case fw::VirtualKey::Num3: target[o] = 0x26; break;
		case fw::VirtualKey::Num4: target[o] = 0x25; break;
		case fw::VirtualKey::Num5: target[o] = 0x2E; break;
		case fw::VirtualKey::Num6: target[o] = 0x36; break;
		case fw::VirtualKey::Num7: target[o] = 0x3D; break;
		case fw::VirtualKey::Num8: target[o] = 0x3E; break;
		case fw::VirtualKey::Num9: target[o] = 0x46; break;

		case fw::VirtualKey::A: target[o] = 0x1C; break;
		case fw::VirtualKey::B: target[o] = 0x32; break;
		case fw::VirtualKey::C: target[o] = 0x21; break;
		case fw::VirtualKey::D: target[o] = 0x23; break;
		case fw::VirtualKey::E: target[o] = 0x24; break;
		case fw::VirtualKey::F: target[o] = 0x2B; break;
		case fw::VirtualKey::G: target[o] = 0x34; break;
		case fw::VirtualKey::H: target[o] = 0x33; break;
		case fw::VirtualKey::I: target[o] = 0x43; break;
		case fw::VirtualKey::J: target[o] = 0x3B; break;
		case fw::VirtualKey::K: target[o] = 0x42; break;
		case fw::VirtualKey::L: target[o] = 0x4B; break;
		case fw::VirtualKey::M: target[o] = 0x3A; break;
		case fw::VirtualKey::N: target[o] = 0x31; break;
		case fw::VirtualKey::O: target[o] = 0x44; break;
		case fw::VirtualKey::P: target[o] = 0x4D; break;
		case fw::VirtualKey::Q: target[o] = 0x15; break;
		case fw::VirtualKey::R: target[o] = 0x2D; break;
		case fw::VirtualKey::S: target[o] = 0x1B; break;
		case fw::VirtualKey::T: target[o] = 0x2C; break;
		case fw::VirtualKey::U: target[o] = 0x3C; break;
		case fw::VirtualKey::V: target[o] = 0x2A; break;
		case fw::VirtualKey::W: target[o] = 0x1D; break;
		case fw::VirtualKey::X: target[o] = 0x22; break;
		case fw::VirtualKey::Y: target[o] = 0x35; break;
		case fw::VirtualKey::Z: target[o] = 0x1A; break;

		case fw::VirtualKey::PrintScreen:
			target[o] = 0x12;
			target[o + 1] = 0xE0;
			target[o + 2] = 0x7C;
			return 3;

		case fw::VirtualKey::Pause:
			target[o] = 0xE1;
			target[o + 1] = 0x14;
			target[o + 2] = 0x77;
			target[o + 3] = 0xE1;
			target[o + 4] = 0xF0;
			target[o + 5] = 0x14;
			target[o + 6] = 0xF0;
			target[o + 7] = 0x77;
			return 8;
		}

		if (target[o] != 0) {
			return o + 1;
		}

		return 0;
	}

	int Ps2Util::getBreakCode(fw::VirtualKey vk, uint8_t* target) {
		if (vk == fw::VirtualKey::Pause) {
			return 0;
		}

		int o = writeExtended(vk, target);
		int count = getMakeCode(vk, target + o + 1, false);
		if (count > 0) {
			target[o] = 0xF0;
			return o + count + 1;
		}

		return 0;
	}
}
