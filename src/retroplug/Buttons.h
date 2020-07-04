#pragma once

#include <string>
#include <map>

namespace ButtonTypes {
	enum ButtonType : unsigned int {
		Right,
		Left,
		Up,
		Down,
		A,
		B,
		Select,
		Start,
		MAX
	};

	const std::map<std::string, ButtonType> Lookup = {
		{ "Left", ButtonTypes::Left },
		{ "Up", ButtonTypes::Up },
		{ "Right", ButtonTypes::Right },
		{ "Down", ButtonTypes::Down },
		{ "A", ButtonTypes::A },
		{ "B", ButtonTypes::B },
		{ "Start", ButtonTypes::Start },
		{ "Select", ButtonTypes::Select }
	};

	static ButtonType fromString(const std::string& name) {
		auto found = Lookup.find(name);
		if (found != Lookup.end()) {
			return found->second;
		}

		return ButtonType::MAX;
	}

	static std::string toString(ButtonType button) {
		switch (button) {
		case ButtonType::Left: return "Left";
		case ButtonType::Up: return "Up";
		case ButtonType::Right: return "Right";
		case ButtonType::Down: return "Down";
		case ButtonType::A: return "A";
		case ButtonType::B: return "B";
		case ButtonType::Start: return "Start";
		case ButtonType::Select: return "Select";
		}

		return "";
	}
}

typedef ButtonTypes::ButtonType ButtonType;
