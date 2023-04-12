#pragma once

#include "foundation/Input.h"

namespace rp::Ps2Util {
	int writeExtended(VirtualKey::Enum vk, uint8_t* target) {
		switch (vk) {
		case VirtualKey::LeftWin:
		case VirtualKey::RightCtrl:
		case VirtualKey::RightWin:
		case VirtualKey::Insert:
		case VirtualKey::Home:
		case VirtualKey::Delete:
		case VirtualKey::End:
		case VirtualKey::Divide:
		case VirtualKey::LeftArrow:
		case VirtualKey::RightArrow:
		case VirtualKey::UpArrow:
		case VirtualKey::DownArrow:
		case VirtualKey::PageUp:
		case VirtualKey::PageDown:
		case VirtualKey::Sleep:
		case VirtualKey::PrintScreen:
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

	int getMakeCode(VirtualKey::Enum vk, uint8_t* target, bool includeExt) {
		int o = 0;
		if (includeExt) {
			o = writeExtended(vk, target);
		}

		target[o] = 0;

		switch (vk) {

		case VirtualKey::Esc: target[o] = 0x76; break;
		case VirtualKey::F1: target[o] = 0x05; break;
		case VirtualKey::F2: target[o] = 0x06; break;
		case VirtualKey::F3: target[o] = 0x04; break;
		case VirtualKey::F4: target[o] = 0x0C; break;
		case VirtualKey::F5: target[o] = 0x03; break;
		case VirtualKey::F6: target[o] = 0x0B; break;
		case VirtualKey::F7: target[o] = 0x83; break;
		case VirtualKey::F8: target[o] = 0x0A; break;
		case VirtualKey::F9: target[o] = 0x01; break;
		case VirtualKey::F10: target[o] = 0x09; break;
		case VirtualKey::F11: target[o] = 0x78; break;
		case VirtualKey::F12: target[o] = 0x07; break;
		case VirtualKey::Space: target[o] = 0x29; break;
		case VirtualKey::Enter: target[o] = 0x5A; break;

		case VirtualKey::Oem1: target[o] = 0x4C; break; // ;
		case VirtualKey::Oem2: target[o] = 0x4A; break; // /
		case VirtualKey::Oem3: target[o] = 0x0E; break; // `
		case VirtualKey::Oem4: target[o] = 0x54; break; // [
		case VirtualKey::Oem5: target[o] = 0x5D; break; // \ (backslash)
		case VirtualKey::Oem6: target[o] = 0x5B; break; // ]
		case VirtualKey::Oem7: target[o] = 0x52; break; // '

		case VirtualKey::OemMinus: target[o] = 0x4E; break;
		case VirtualKey::OemPlus: target[o] = 0x55; break;
		case VirtualKey::OemPeriod: target[o] = 0x49; break;
		case VirtualKey::OemComma: target[o] = 0x41; break;

		case VirtualKey::Subtract: target[o] = 0x7B; break;
		case VirtualKey::Add: target[o] = 0x79; break;
		case VirtualKey::Divide: target[o] = 0x4A; break;
		case VirtualKey::Multiply: target[o] = 0x7C; break;
		case VirtualKey::Decimal: target[o] = 0x71; break;

		case VirtualKey::NumPad0: target[o] = 0x70; break;
		case VirtualKey::NumPad1: target[o] = 0x69; break;
		case VirtualKey::NumPad2: target[o] = 0x72; break;
		case VirtualKey::NumPad3: target[o] = 0x7A; break;
		case VirtualKey::NumPad4: target[o] = 0x6B; break;
		case VirtualKey::NumPad5: target[o] = 0x73; break;
		case VirtualKey::NumPad6: target[o] = 0x74; break;
		case VirtualKey::NumPad7: target[o] = 0x6C; break;
		case VirtualKey::NumPad8: target[o] = 0x75; break;
		case VirtualKey::NumPad9: target[o] = 0x7D; break;

		case VirtualKey::Backspace: target[o] = 0x66; break;
		case VirtualKey::Tab: target[o] = 0x0D; break;
		case VirtualKey::Caps: target[o] = 0x58; break;

		case VirtualKey::Ctrl: target[o] = 0x14; break;
		case VirtualKey::Shift: target[o] = 0x12; break;
		case VirtualKey::Alt: target[o] = 0x11; break;

		case VirtualKey::LeftShift: target[o] = 0x12; break;
		case VirtualKey::LeftCtrl: target[o] = 0x14; break;
		case VirtualKey::LeftWin: target[o] = 0x1F; break;

		case VirtualKey::RightShift: target[o] = 0x59; break;
		case VirtualKey::RightCtrl: target[o] = 0x1F; break;
		case VirtualKey::RightWin: target[o] = 0x1F; break;


		case VirtualKey::Scroll: target[o] = 0x7E; break;
		case VirtualKey::Insert: target[o] = 0x70; break;

		case VirtualKey::Home: target[o] = 0x6C; break;
		case VirtualKey::Delete: target[o] = 0x71; break;
		case VirtualKey::End: target[o] = 0x69; break;

		case VirtualKey::NumLock: target[o] = 0x77; break;

		case VirtualKey::LeftArrow: target[o] = 0x6B; break;
		case VirtualKey::RightArrow: target[o] = 0x74; break;
		case VirtualKey::UpArrow: target[o] = 0x75; break;
		case VirtualKey::DownArrow: target[o] = 0x72; break;
		case VirtualKey::PageUp: target[o] = 0x7D; break;
		case VirtualKey::PageDown: target[o] = 0x7A; break;

		case VirtualKey::Num0: target[o] = 0x45; break;
		case VirtualKey::Num1: target[o] = 0x16; break;
		case VirtualKey::Num2: target[o] = 0x1E; break;
		case VirtualKey::Num3: target[o] = 0x26; break;
		case VirtualKey::Num4: target[o] = 0x25; break;
		case VirtualKey::Num5: target[o] = 0x2E; break;
		case VirtualKey::Num6: target[o] = 0x36; break;
		case VirtualKey::Num7: target[o] = 0x3D; break;
		case VirtualKey::Num8: target[o] = 0x3E; break;
		case VirtualKey::Num9: target[o] = 0x46; break;

		case VirtualKey::A: target[o] = 0x1C; break;
		case VirtualKey::B: target[o] = 0x32; break;
		case VirtualKey::C: target[o] = 0x21; break;
		case VirtualKey::D: target[o] = 0x23; break;
		case VirtualKey::E: target[o] = 0x24; break;
		case VirtualKey::F: target[o] = 0x2B; break;
		case VirtualKey::G: target[o] = 0x34; break;
		case VirtualKey::H: target[o] = 0x33; break;
		case VirtualKey::I: target[o] = 0x43; break;
		case VirtualKey::J: target[o] = 0x3B; break;
		case VirtualKey::K: target[o] = 0x42; break;
		case VirtualKey::L: target[o] = 0x4B; break;
		case VirtualKey::M: target[o] = 0x3A; break;
		case VirtualKey::N: target[o] = 0x31; break;
		case VirtualKey::O: target[o] = 0x44; break;
		case VirtualKey::P: target[o] = 0x4D; break;
		case VirtualKey::Q: target[o] = 0x15; break;
		case VirtualKey::R: target[o] = 0x2D; break;
		case VirtualKey::S: target[o] = 0x1B; break;
		case VirtualKey::T: target[o] = 0x2C; break;
		case VirtualKey::U: target[o] = 0x3C; break;
		case VirtualKey::V: target[o] = 0x2A; break;
		case VirtualKey::W: target[o] = 0x1D; break;
		case VirtualKey::X: target[o] = 0x22; break;
		case VirtualKey::Y: target[o] = 0x35; break;
		case VirtualKey::Z: target[o] = 0x1A; break;

		case VirtualKey::PrintScreen:
			target[o] = 0x12;
			target[o + 1] = 0xE0;
			target[o + 2] = 0x7C;
			return 3;

		case VirtualKey::Pause:
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

	int getBreakCode(VirtualKey::Enum vk, uint8_t* target) {
		if (vk == VirtualKey::Pause) {
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