#pragma once

#include <string_view>
#include <unordered_map>
#include "foundation/Math.h"

namespace fw {
	enum class MouseButton : unsigned int {
		Unknown,
		Left,
		Right,
		Middle,
		COUNT
	};

	namespace MouseButtonUtil {
		static const std::unordered_map<std::string_view, MouseButton> Lookup = {
			{ "Left", MouseButton::Left },
			{ "Right", MouseButton::Right },
			{ "Middle", MouseButton::Middle }
		};

		static MouseButton fromString(std::string_view key) {
			auto found = Lookup.find(key);
			if (found != Lookup.end()) {
				return found->second;
			}

			return MouseButton::Unknown;
		}

		static std::string_view toString(MouseButton idx) {
			for (auto& key : Lookup) {
				if (key.second == idx) {
					return key.first;
				}
			}

			return "Unknown";
		}
	}

	enum class ButtonType : unsigned int {
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

	namespace ButtonTypeUtil {
		const std::unordered_map<std::string_view, ButtonType> Lookup = {
			{ "Left", ButtonType::Left },
			{ "Up", ButtonType::Up },
			{ "Right", ButtonType::Right },
			{ "Down", ButtonType::Down },
			{ "A", ButtonType::A },
			{ "B", ButtonType::B },
			{ "Start", ButtonType::Start },
			{ "Select", ButtonType::Select }
		};

		static ButtonType fromString(std::string_view name) {
			auto found = Lookup.find(name);
			if (found != Lookup.end()) {
				return found->second;
			}

			return ButtonType::MAX;
		}

		static std::string_view toString(ButtonType button) {
			switch (button) {
			case ButtonType::Left: return "Left";
			case ButtonType::Up: return "Up";
			case ButtonType::Right: return "Right";
			case ButtonType::Down: return "Down";
			case ButtonType::A: return "A";
			case ButtonType::B: return "B";
			case ButtonType::Start: return "Start";
			case ButtonType::Select: return "Select";
			default: return "";
			}
		}
	}

	enum class VirtualKey : unsigned int {
		Unknown = 0x00,
		Backspace = 0x08,
		Tab = 0x09,
		Clear = 0x0C,
		Enter = 0x0D,
		Shift = 0x10,
		Ctrl = 0x11,
		Alt = 0x12,
		Pause = 0x13,
		Caps = 0x14,
		Esc = 0x1B,
		Space = 0x20,
		PageUp = 0x21,
		PageDown = 0x22,
		End = 0x23,
		Home = 0x24,
		LeftArrow = 0x25,
		UpArrow = 0x26,
		RightArrow = 0x27,
		DownArrow = 0x28,
		Select = 0x29,
		Print = 0x2A,
		Execute = 0x2B,
		PrintScreen = 0x2C,
		Insert = 0x2D,
		Delete = 0x2E,
		Help = 0x2F,
		Num0 = 0x30,
		Num1 = 0x31,
		Num2 = 0x32,
		Num3 = 0x33,
		Num4 = 0x34,
		Num5 = 0x35,
		Num6 = 0x36,
		Num7 = 0x37,
		Num8 = 0x38,
		Num9 = 0x39,
		A = 0x41,
		B = 0x42,
		C = 0x43,
		D = 0x44,
		E = 0x45,
		F = 0x46,
		G = 0x47,
		H = 0x48,
		I = 0x49,
		J = 0x4A,
		K = 0x4B,
		L = 0x4C,
		M = 0x4D,
		N = 0x4E,
		O = 0x4F,
		P = 0x50,
		Q = 0x51,
		R = 0x52,
		S = 0x53,
		T = 0x54,
		U = 0x55,
		V = 0x56,
		W = 0x57,
		X = 0x58,
		Y = 0x59,
		Z = 0x5A,
		LeftWin = 0x5B,
		RightWin = 0x5C,
		Sleep = 0x5F,
		NumPad0 = 0x60,
		NumPad1 = 0x61,
		NumPad2 = 0x62,
		NumPad3 = 0x63,
		NumPad4 = 0x64,
		NumPad5 = 0x65,
		NumPad6 = 0x66,
		NumPad7 = 0x67,
		NumPad8 = 0x68,
		NumPad9 = 0x69,
		Multiply = 0x6A,
		Add = 0x6B,
		Separator = 0x6C,
		Subtract = 0x6D,
		Decimal = 0x6E,
		Divide = 0x6F,
		F1 = 0x70,
		F2 = 0x71,
		F3 = 0x72,
		F4 = 0x73,
		F5 = 0x74,
		F6 = 0x75,
		F7 = 0x76,
		F8 = 0x77,
		F9 = 0x78,
		F10 = 0x79,
		F11 = 0x7A,
		F12 = 0x7B,
		F13 = 0x7C,
		F14 = 0x7D,
		F15 = 0x7E,
		F16 = 0x7F,
		F17 = 0x80,
		F18 = 0x81,
		F19 = 0x82,
		F20 = 0x83,
		F21 = 0x84,
		F22 = 0x85,
		F23 = 0x86,
		F24 = 0x87,
		NumLock = 0x90,
		Scroll = 0x91,
		LeftShift = 0xA0,
		RightShift = 0xA1,
		LeftCtrl = 0xA2,
		RightCtrl = 0xA3,
		LeftMenu = 0xA4,
		RightMenu = 0xA5,

		Oem1 = 0xBA,
		Oem2 = 0xBF,
		Oem3 = 0xC0,
		Oem4 = 0xDB,
		Oem5 = 0xDC,
		Oem6 = 0xDD,
		Oem7 = 0xDE,
		Oem8 = 0xDF,

		OemPlus = 0xBB,
		OemComma = 0xBC,
		OemMinus = 0xBD,
		OemPeriod = 0xBE,

		COUNT
	};

	namespace VirtualKeyUtil {
		static const std::unordered_map<std::string_view, VirtualKey> Lookup = {
			{ "Backspace", VirtualKey::Backspace },
			{ "Tab", VirtualKey::Tab },
			{ "Clear", VirtualKey::Clear },
			{ "Enter", VirtualKey::Enter },
			{ "Shift", VirtualKey::Shift },
			{ "Ctrl", VirtualKey::Ctrl },
			{ "Alt", VirtualKey::Alt },
			{ "Pause", VirtualKey::Pause },
			{ "Caps", VirtualKey::Caps },
			{ "Esc", VirtualKey::Esc },
			{ "Space", VirtualKey::Space },
			{ "PageUp", VirtualKey::PageUp },
			{ "PageDown", VirtualKey::PageDown },
			{ "End", VirtualKey::End },
			{ "Home", VirtualKey::Home },
			{ "LeftArrow", VirtualKey::LeftArrow },
			{ "UpArrow", VirtualKey::UpArrow },
			{ "RightArrow", VirtualKey::RightArrow },
			{ "DownArrow", VirtualKey::DownArrow },
			{ "Select", VirtualKey::Select },
			{ "Print", VirtualKey::Print },
			{ "Execute", VirtualKey::Execute },
			{ "PrintScreen", VirtualKey::PrintScreen },
			{ "Insert", VirtualKey::Insert },
			{ "Delete", VirtualKey::Delete },
			{ "Help", VirtualKey::Help },
			{ "0", VirtualKey::Num0 },
			{ "1", VirtualKey::Num1 },
			{ "2", VirtualKey::Num2 },
			{ "3", VirtualKey::Num3 },
			{ "4", VirtualKey::Num4 },
			{ "5", VirtualKey::Num5 },
			{ "6", VirtualKey::Num6 },
			{ "7", VirtualKey::Num7 },
			{ "8", VirtualKey::Num8 },
			{ "9", VirtualKey::Num9 },
			{ "A", VirtualKey::A },
			{ "B", VirtualKey::B },
			{ "C", VirtualKey::C },
			{ "D", VirtualKey::D },
			{ "E", VirtualKey::E },
			{ "F", VirtualKey::F },
			{ "G", VirtualKey::G },
			{ "H", VirtualKey::H },
			{ "I", VirtualKey::I },
			{ "J", VirtualKey::J },
			{ "K", VirtualKey::K },
			{ "L", VirtualKey::L },
			{ "M", VirtualKey::M },
			{ "N", VirtualKey::N },
			{ "O", VirtualKey::O },
			{ "P", VirtualKey::P },
			{ "Q", VirtualKey::Q },
			{ "R", VirtualKey::R },
			{ "S", VirtualKey::S },
			{ "T", VirtualKey::T },
			{ "U", VirtualKey::U },
			{ "V", VirtualKey::V },
			{ "W", VirtualKey::W },
			{ "X", VirtualKey::X },
			{ "Y", VirtualKey::Y },
			{ "Z", VirtualKey::Z },
			{ "LeftWin", VirtualKey::LeftWin },
			{ "RightWin", VirtualKey::RightWin },
			{ "Sleep", VirtualKey::Sleep },
			{ "NumPad0", VirtualKey::NumPad0 },
			{ "NumPad1", VirtualKey::NumPad1 },
			{ "NumPad2", VirtualKey::NumPad2 },
			{ "NumPad3", VirtualKey::NumPad3 },
			{ "NumPad4", VirtualKey::NumPad4 },
			{ "NumPad5", VirtualKey::NumPad5 },
			{ "NumPad6", VirtualKey::NumPad6 },
			{ "NumPad7", VirtualKey::NumPad7 },
			{ "NumPad8", VirtualKey::NumPad8 },
			{ "NumPad9", VirtualKey::NumPad9 },
			{ "Multiply", VirtualKey::Multiply },
			{ "Add", VirtualKey::Add },
			{ "Separator", VirtualKey::Separator },
			{ "Subtract", VirtualKey::Subtract },
			{ "Decimal", VirtualKey::Decimal },
			{ "Divide", VirtualKey::Divide },
			{ "F1", VirtualKey::F1 },
			{ "F2", VirtualKey::F2 },
			{ "F3", VirtualKey::F3 },
			{ "F4", VirtualKey::F4 },
			{ "F5", VirtualKey::F5 },
			{ "F6", VirtualKey::F6 },
			{ "F7", VirtualKey::F7 },
			{ "F8", VirtualKey::F8 },
			{ "F9", VirtualKey::F9 },
			{ "F10", VirtualKey::F10 },
			{ "F11", VirtualKey::F11 },
			{ "F12", VirtualKey::F12 },
			{ "F13", VirtualKey::F13 },
			{ "F14", VirtualKey::F14 },
			{ "F15", VirtualKey::F15 },
			{ "F16", VirtualKey::F16 },
			{ "F17", VirtualKey::F17 },
			{ "F18", VirtualKey::F18 },
			{ "F19", VirtualKey::F19 },
			{ "F20", VirtualKey::F20 },
			{ "F21", VirtualKey::F21 },
			{ "F22", VirtualKey::F22 },
			{ "F23", VirtualKey::F23 },
			{ "F24", VirtualKey::F24 },
			{ "NumLock", VirtualKey::NumLock },
			{ "Scroll", VirtualKey::Scroll },
			{ "LeftShift", VirtualKey::LeftShift },
			{ "RightShift", VirtualKey::RightShift },
			{ "LeftCtrl", VirtualKey::LeftCtrl },
			{ "RightCtrl", VirtualKey::RightCtrl },
			{ "LeftMenu", VirtualKey::LeftMenu },
			{ "RightMenu", VirtualKey::RightMenu },
			{ "Oem1", VirtualKey::Oem1 },
			{ "Oem2", VirtualKey::Oem2 },
			{ "Oem3", VirtualKey::Oem3 },
			{ "Oem4", VirtualKey::Oem4 },
			{ "Oem5", VirtualKey::Oem5 },
			{ "Oem6", VirtualKey::Oem6 },
			{ "Oem7", VirtualKey::Oem7 },
			{ "Oem8", VirtualKey::Oem8 },
			{ "OemPlus", VirtualKey::OemPlus },
			{ "OemComma", VirtualKey::OemComma },
			{ "OemMinus", VirtualKey::OemMinus },
			{ "OemPeriod", VirtualKey::OemPeriod },
		};

		static VirtualKey fromString(std::string_view key) {
			auto found = Lookup.find(key);
			if (found != Lookup.end()) {
				return found->second;
			}

			return VirtualKey::Unknown;
		}

		static std::string_view toString(VirtualKey idx) {
			for (auto& key : Lookup) {
				if (key.second == idx) {
					return key.first;
				}
			}

			return "Unknown";
		}
	}

	enum class KeyAction {
		Release,
		Press,
		Repeat
	};

	struct KeyEvent {
		VirtualKey key;
		KeyAction action;
		bool down;
	};

	struct CharEvent {
		uint32 keyCode = 0;
	};

	struct MouseScrollEvent {
		PointF delta;
		Point position;
	};

	struct MouseButtonEvent {
		MouseButton button;
		bool down;
		Point position;
	};

	struct MouseFocusEvent {};

	struct MouseBlurEvent {};

	struct MouseDoubleClickEvent {
		MouseButton button;
		Point position;
	};

	struct ButtonEvent {
		ButtonType button;
		bool down;
	};

	struct MouseEnterEvent {
		Point position;
	};

	struct MouseLeaveEvent {};

	struct MouseMoveEvent {
		Point position;
	};
}

#include "foundation/MathMeta.h"

REFL_AUTO(
	type(fw::MouseMoveEvent),
	field(position)
)

REFL_AUTO(
	type(fw::MouseEnterEvent),
	field(position)
)

REFL_AUTO(
	type(fw::MouseLeaveEvent)
)

REFL_AUTO(
	type(fw::MouseFocusEvent)
)

REFL_AUTO(
	type(fw::MouseBlurEvent)
)

REFL_AUTO(
	type(fw::MouseButtonEvent),
	field(button),
	field(down),
	field(position)
)
