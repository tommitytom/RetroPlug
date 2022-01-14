#pragma once

#include "core/Input.h"

namespace rp {
	static ButtonType::Enum keyToButton(VirtualKey::Enum key) {
		ButtonType::Enum button = ButtonType::MAX;

		switch (key) {
		case VirtualKey::LeftArrow: button = ButtonType::Left; break;
		case VirtualKey::UpArrow: button = ButtonType::Up; break;
		case VirtualKey::RightArrow: button = ButtonType::Right; break;
		case VirtualKey::DownArrow: button = ButtonType::Down; break;
		case VirtualKey::D: button = ButtonType::A; break;
		case VirtualKey::W: button = ButtonType::B; break;
		case VirtualKey::Enter: button = ButtonType::Start; break;
		case VirtualKey::Space: button = ButtonType::Start; break;
		case VirtualKey::LeftCtrl: button = ButtonType::Select; break;
		}

		return button;
	}
}