#include "Ps2Util.h"

namespace rp {
	int Ps2Util::writeExtended(orb::VirtualKey vk, uint8_t* target) {
		switch (vk) {
		case orb::VirtualKey::LeftWin:
		case orb::VirtualKey::RightCtrl:
		case orb::VirtualKey::RightWin:
		case orb::VirtualKey::Insert:
		case orb::VirtualKey::Home:
		case orb::VirtualKey::Delete:
		case orb::VirtualKey::End:
		case orb::VirtualKey::Divide:
		case orb::VirtualKey::LeftArrow:
		case orb::VirtualKey::RightArrow:
		case orb::VirtualKey::UpArrow:
		case orb::VirtualKey::DownArrow:
		case orb::VirtualKey::PageUp:
		case orb::VirtualKey::PageDown:
		case orb::VirtualKey::Sleep:
		case orb::VirtualKey::PrintScreen:
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

	int Ps2Util::getMakeCode(orb::VirtualKey vk, uint8_t* target, bool includeExt) {
		int o = 0;
		if (includeExt) {
			o = writeExtended(vk, target);
		}

		target[o] = 0;

		switch (vk) {

		case orb::VirtualKey::Esc: target[o] = 0x76; break;
		case orb::VirtualKey::F1: target[o] = 0x05; break;
		case orb::VirtualKey::F2: target[o] = 0x06; break;
		case orb::VirtualKey::F3: target[o] = 0x04; break;
		case orb::VirtualKey::F4: target[o] = 0x0C; break;
		case orb::VirtualKey::F5: target[o] = 0x03; break;
		case orb::VirtualKey::F6: target[o] = 0x0B; break;
		case orb::VirtualKey::F7: target[o] = 0x83; break;
		case orb::VirtualKey::F8: target[o] = 0x0A; break;
		case orb::VirtualKey::F9: target[o] = 0x01; break;
		case orb::VirtualKey::F10: target[o] = 0x09; break;
		case orb::VirtualKey::F11: target[o] = 0x78; break;
		case orb::VirtualKey::F12: target[o] = 0x07; break;
		case orb::VirtualKey::Space: target[o] = 0x29; break;
		case orb::VirtualKey::Enter: target[o] = 0x5A; break;

		case orb::VirtualKey::Oem1: target[o] = 0x4C; break; // ;
		case orb::VirtualKey::Oem2: target[o] = 0x4A; break; // /
		case orb::VirtualKey::Oem3: target[o] = 0x0E; break; // `
		case orb::VirtualKey::Oem4: target[o] = 0x54; break; // [
		case orb::VirtualKey::Oem5: target[o] = 0x5D; break; // \ (backslash)
		case orb::VirtualKey::Oem6: target[o] = 0x5B; break; // ]
		case orb::VirtualKey::Oem7: target[o] = 0x52; break; // '

		case orb::VirtualKey::OemMinus: target[o] = 0x4E; break;
		case orb::VirtualKey::OemPlus: target[o] = 0x55; break;
		case orb::VirtualKey::OemPeriod: target[o] = 0x49; break;
		case orb::VirtualKey::OemComma: target[o] = 0x41; break;

		case orb::VirtualKey::Subtract: target[o] = 0x7B; break;
		case orb::VirtualKey::Add: target[o] = 0x79; break;
		case orb::VirtualKey::Divide: target[o] = 0x4A; break;
		case orb::VirtualKey::Multiply: target[o] = 0x7C; break;
		case orb::VirtualKey::Decimal: target[o] = 0x71; break;

		case orb::VirtualKey::NumPad0: target[o] = 0x70; break;
		case orb::VirtualKey::NumPad1: target[o] = 0x69; break;
		case orb::VirtualKey::NumPad2: target[o] = 0x72; break;
		case orb::VirtualKey::NumPad3: target[o] = 0x7A; break;
		case orb::VirtualKey::NumPad4: target[o] = 0x6B; break;
		case orb::VirtualKey::NumPad5: target[o] = 0x73; break;
		case orb::VirtualKey::NumPad6: target[o] = 0x74; break;
		case orb::VirtualKey::NumPad7: target[o] = 0x6C; break;
		case orb::VirtualKey::NumPad8: target[o] = 0x75; break;
		case orb::VirtualKey::NumPad9: target[o] = 0x7D; break;

		case orb::VirtualKey::Backspace: target[o] = 0x66; break;
		case orb::VirtualKey::Tab: target[o] = 0x0D; break;
		case orb::VirtualKey::Caps: target[o] = 0x58; break;

		case orb::VirtualKey::Ctrl: target[o] = 0x14; break;
		case orb::VirtualKey::Shift: target[o] = 0x12; break;
		case orb::VirtualKey::Alt: target[o] = 0x11; break;

		case orb::VirtualKey::LeftShift: target[o] = 0x12; break;
		case orb::VirtualKey::LeftCtrl: target[o] = 0x14; break;
		case orb::VirtualKey::LeftWin: target[o] = 0x1F; break;

		case orb::VirtualKey::RightShift: target[o] = 0x59; break;
		case orb::VirtualKey::RightCtrl: target[o] = 0x1F; break;
		case orb::VirtualKey::RightWin: target[o] = 0x1F; break;


		case orb::VirtualKey::Scroll: target[o] = 0x7E; break;
		case orb::VirtualKey::Insert: target[o] = 0x70; break;

		case orb::VirtualKey::Home: target[o] = 0x6C; break;
		case orb::VirtualKey::Delete: target[o] = 0x71; break;
		case orb::VirtualKey::End: target[o] = 0x69; break;

		case orb::VirtualKey::NumLock: target[o] = 0x77; break;

		case orb::VirtualKey::LeftArrow: target[o] = 0x6B; break;
		case orb::VirtualKey::RightArrow: target[o] = 0x74; break;
		case orb::VirtualKey::UpArrow: target[o] = 0x75; break;
		case orb::VirtualKey::DownArrow: target[o] = 0x72; break;
		case orb::VirtualKey::PageUp: target[o] = 0x7D; break;
		case orb::VirtualKey::PageDown: target[o] = 0x7A; break;

		case orb::VirtualKey::Num0: target[o] = 0x45; break;
		case orb::VirtualKey::Num1: target[o] = 0x16; break;
		case orb::VirtualKey::Num2: target[o] = 0x1E; break;
		case orb::VirtualKey::Num3: target[o] = 0x26; break;
		case orb::VirtualKey::Num4: target[o] = 0x25; break;
		case orb::VirtualKey::Num5: target[o] = 0x2E; break;
		case orb::VirtualKey::Num6: target[o] = 0x36; break;
		case orb::VirtualKey::Num7: target[o] = 0x3D; break;
		case orb::VirtualKey::Num8: target[o] = 0x3E; break;
		case orb::VirtualKey::Num9: target[o] = 0x46; break;

		case orb::VirtualKey::A: target[o] = 0x1C; break;
		case orb::VirtualKey::B: target[o] = 0x32; break;
		case orb::VirtualKey::C: target[o] = 0x21; break;
		case orb::VirtualKey::D: target[o] = 0x23; break;
		case orb::VirtualKey::E: target[o] = 0x24; break;
		case orb::VirtualKey::F: target[o] = 0x2B; break;
		case orb::VirtualKey::G: target[o] = 0x34; break;
		case orb::VirtualKey::H: target[o] = 0x33; break;
		case orb::VirtualKey::I: target[o] = 0x43; break;
		case orb::VirtualKey::J: target[o] = 0x3B; break;
		case orb::VirtualKey::K: target[o] = 0x42; break;
		case orb::VirtualKey::L: target[o] = 0x4B; break;
		case orb::VirtualKey::M: target[o] = 0x3A; break;
		case orb::VirtualKey::N: target[o] = 0x31; break;
		case orb::VirtualKey::O: target[o] = 0x44; break;
		case orb::VirtualKey::P: target[o] = 0x4D; break;
		case orb::VirtualKey::Q: target[o] = 0x15; break;
		case orb::VirtualKey::R: target[o] = 0x2D; break;
		case orb::VirtualKey::S: target[o] = 0x1B; break;
		case orb::VirtualKey::T: target[o] = 0x2C; break;
		case orb::VirtualKey::U: target[o] = 0x3C; break;
		case orb::VirtualKey::V: target[o] = 0x2A; break;
		case orb::VirtualKey::W: target[o] = 0x1D; break;
		case orb::VirtualKey::X: target[o] = 0x22; break;
		case orb::VirtualKey::Y: target[o] = 0x35; break;
		case orb::VirtualKey::Z: target[o] = 0x1A; break;

		case orb::VirtualKey::PrintScreen:
			target[o] = 0x12;
			target[o + 1] = 0xE0;
			target[o + 2] = 0x7C;
			return 3;

		case orb::VirtualKey::Pause:
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

	int Ps2Util::getBreakCode(orb::VirtualKey vk, uint8_t* target) {
		if (vk == orb::VirtualKey::Pause) {
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
