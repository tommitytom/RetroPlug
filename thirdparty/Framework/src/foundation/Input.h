#pragma once

#include <initializer_list>
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
		const std::initializer_list<std::pair<std::string_view, ButtonType>> Items = {
			{ "Left", ButtonType::Left },
			{ "Up", ButtonType::Up },
			{ "Right", ButtonType::Right },
			{ "Down", ButtonType::Down },
			{ "A", ButtonType::A },
			{ "B", ButtonType::B },
			{ "Start", ButtonType::Start },
			{ "Select", ButtonType::Select }
		};

		const std::unordered_map<std::string_view, ButtonType> Lookup = { Items.begin(), Items.end() };

		static ButtonType fromString(std::string_view name) {
			auto found = Lookup.find(name);
			if (found != Lookup.end()) {
				return found->second;
			}

			return ButtonType::MAX;
		}

		static std::string_view toString(ButtonType button) {
			for (const auto& [name, type] : Lookup) {
				if (type == button) {
					return name;
				}
			}
			return "";
		}
	}

	enum class PadButtonType : unsigned int {
		//Axis converted to button presses
		LeftStickLeft,
		LeftStickRight,
		LeftStickDown,
		LeftStickUp,
		RightStickLeft,
		RightStickRight,
		RightStickDown,
		RightStickUp,

		//Actual button presses
		Start,
		Select,
		Left,
		Right,
		Up,
		Down,
		A,
		B,
		X,
		Y,
		L1,
		R1,
		L2,
		R2,
		L3,
		R3,
		Home,
		Button17,
		Button18,
		Button19,
		Button20,
		Button21,
		Button22,
		Button23,
		Button24,
		Button25,
		Button26,
		Button27,
		Button28,
		Button29,
		Button30,
		Button31,

		COUNT
	};

	namespace PadButtonTypeUtil {
		const std::initializer_list<std::pair<std::string_view, PadButtonType>> Items = {
			{ "LeftStickLeft", PadButtonType::LeftStickLeft },
			{ "LeftStickRight", PadButtonType::LeftStickRight },
			{ "LeftStickDown", PadButtonType::LeftStickDown },
			{ "LeftStickUp", PadButtonType::LeftStickUp },
			{ "RightStickLeft", PadButtonType::RightStickLeft },
			{ "RightStickRight", PadButtonType::RightStickRight },
			{ "RightStickDown", PadButtonType::RightStickDown },
			{ "RightStickUp", PadButtonType::RightStickUp },
			{ "Start", PadButtonType::Start },
			{ "Select", PadButtonType::Select },
			{ "Left", PadButtonType::Left },
			{ "Right", PadButtonType::Right },
			{ "Up", PadButtonType::Up },
			{ "Down", PadButtonType::Down },
			{ "A", PadButtonType::A },
			{ "B", PadButtonType::B },
			{ "X", PadButtonType::X },
			{ "Y", PadButtonType::Y },
			{ "L1", PadButtonType::L1 },
			{ "R1", PadButtonType::R1 },
			{ "L2", PadButtonType::L2 },
			{ "R2", PadButtonType::R2 },
			{ "L3", PadButtonType::L3 },
			{ "R3", PadButtonType::R3 },
			{ "Home", PadButtonType::Home },
			{ "Button17", PadButtonType::Button17 },
			{ "Button18", PadButtonType::Button18 },
			{ "Button19", PadButtonType::Button19 },
			{ "Button20", PadButtonType::Button20 },
			{ "Button21", PadButtonType::Button21 },
			{ "Button22", PadButtonType::Button22 },
			{ "Button23", PadButtonType::Button23 },
			{ "Button24", PadButtonType::Button24 },
			{ "Button25", PadButtonType::Button25 },
			{ "Button26", PadButtonType::Button26 },
			{ "Button27", PadButtonType::Button27 },
			{ "Button28", PadButtonType::Button28 },
			{ "Button29", PadButtonType::Button29 },
			{ "Button30", PadButtonType::Button30 },
			{ "Button31", PadButtonType::Button31 }
		};

		const std::unordered_map<std::string_view, PadButtonType> Lookup = { Items.begin(), Items.end() };

		static PadButtonType fromString(std::string_view name) {
			auto found = Lookup.find(name);
			if (found != Lookup.end()) {
				return found->second;
			}

			return PadButtonType::COUNT;
		}

		static std::string_view toString(PadButtonType button) {
			for (const auto& [name, type] : Lookup) {
				if (type == button) {
					return name;
				}
			}
			return "";
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
		const std::initializer_list<std::pair<std::string_view, VirtualKey>> Items = {
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
			{ "Num0", VirtualKey::Num0 },
			{ "Num1", VirtualKey::Num1 },
			{ "Num2", VirtualKey::Num2 },
			{ "Num3", VirtualKey::Num3 },
			{ "Num4", VirtualKey::Num4 },
			{ "Num5", VirtualKey::Num5 },
			{ "Num6", VirtualKey::Num6 },
			{ "Num7", VirtualKey::Num7 },
			{ "Num8", VirtualKey::Num8 },
			{ "Num9", VirtualKey::Num9 },
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

		const std::unordered_map<std::string_view, VirtualKey> Lookup = { Items.begin(), Items.end() };

		static VirtualKey fromString(std::string_view name) {
			auto found = Lookup.find(name);
			if (found != Lookup.end()) {
				return found->second;
			}

			return VirtualKey::COUNT;
		}

		static std::string_view toString(VirtualKey button) {
			for (const auto& [name, type] : Lookup) {
				if (type == button) {
					return name;
				}
			}
			return "";
		}
	}

	// NOTE: Must match 'action' from GLFW
	enum class KeyAction : unsigned int {
		Release,
		Press,
		Repeat
	};

	struct KeyEvent {
		KeyAction action;
		VirtualKey key;
		bool down;

		uint32 action2;
		uint32 key2;
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
