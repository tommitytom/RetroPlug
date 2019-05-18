#pragma once

#include <string>
#include <map>

namespace VirtualKeys {
	enum VirtualKey : unsigned int {
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
		RightMenu = 0xA5
	};

	static const std::map<std::string, VirtualKey> Lookup = {
		{ "Backspace", VirtualKeys::Backspace },
		{ "Tab", VirtualKeys::Tab },
		{ "Clear", VirtualKeys::Clear },
		{ "Enter", VirtualKeys::Enter },
		{ "Shift", VirtualKeys::Shift },
		{ "Ctrl", VirtualKeys::Ctrl },
		{ "Alt", VirtualKeys::Alt },
		{ "Pause", VirtualKeys::Pause },
		{ "Caps", VirtualKeys::Caps },
		{ "Esc", VirtualKeys::Esc },
		{ "Space", VirtualKeys::Space },
		{ "PageUp", VirtualKeys::PageUp },
		{ "PageDown", VirtualKeys::PageDown },
		{ "End", VirtualKeys::End },
		{ "Home", VirtualKeys::Home },
		{ "LeftArrow", VirtualKeys::LeftArrow },
		{ "UpArrow", VirtualKeys::UpArrow },
		{ "RightArrow", VirtualKeys::RightArrow },
		{ "DownArrow", VirtualKeys::DownArrow },
		{ "Select", VirtualKeys::Select },
		{ "Print", VirtualKeys::Print },
		{ "Execute", VirtualKeys::Execute },
		{ "PrintScreen", VirtualKeys::PrintScreen },
		{ "Insert", VirtualKeys::Insert },
		{ "Delete", VirtualKeys::Delete },
		{ "Help", VirtualKeys::Help },
		{ "0", VirtualKeys::Num0 },
		{ "1", VirtualKeys::Num1 },
		{ "2", VirtualKeys::Num2 },
		{ "3", VirtualKeys::Num3 },
		{ "4", VirtualKeys::Num4 },
		{ "5", VirtualKeys::Num5 },
		{ "6", VirtualKeys::Num6 },
		{ "7", VirtualKeys::Num7 },
		{ "8", VirtualKeys::Num8 },
		{ "9", VirtualKeys::Num9 },
		{ "A", VirtualKeys::A },
		{ "B", VirtualKeys::B },
		{ "C", VirtualKeys::C },
		{ "D", VirtualKeys::D },
		{ "E", VirtualKeys::E },
		{ "F", VirtualKeys::F },
		{ "G", VirtualKeys::G },
		{ "H", VirtualKeys::H },
		{ "I", VirtualKeys::I },
		{ "J", VirtualKeys::J },
		{ "K", VirtualKeys::K },
		{ "L", VirtualKeys::L },
		{ "M", VirtualKeys::M },
		{ "N", VirtualKeys::N },
		{ "O", VirtualKeys::O },
		{ "P", VirtualKeys::P },
		{ "Q", VirtualKeys::Q },
		{ "R", VirtualKeys::R },
		{ "S", VirtualKeys::S },
		{ "T", VirtualKeys::T },
		{ "U", VirtualKeys::U },
		{ "V", VirtualKeys::V },
		{ "W", VirtualKeys::W },
		{ "X", VirtualKeys::X },
		{ "Y", VirtualKeys::Y },
		{ "Z", VirtualKeys::Z },
		{ "LeftWin", VirtualKeys::LeftWin },
		{ "RightWin", VirtualKeys::RightWin },
		{ "Sleep", VirtualKeys::Sleep },
		{ "NumPad0", VirtualKeys::NumPad0 },
		{ "NumPad1", VirtualKeys::NumPad1 },
		{ "NumPad2", VirtualKeys::NumPad2 },
		{ "NumPad3", VirtualKeys::NumPad3 },
		{ "NumPad4", VirtualKeys::NumPad4 },
		{ "NumPad5", VirtualKeys::NumPad5 },
		{ "NumPad6", VirtualKeys::NumPad6 },
		{ "NumPad7", VirtualKeys::NumPad7 },
		{ "NumPad8", VirtualKeys::NumPad8 },
		{ "NumPad9", VirtualKeys::NumPad9 },
		{ "Multiply", VirtualKeys::Multiply },
		{ "Add", VirtualKeys::Add },
		{ "Separator", VirtualKeys::Separator },
		{ "Subtract", VirtualKeys::Subtract },
		{ "Decimal", VirtualKeys::Decimal },
		{ "Divide", VirtualKeys::Divide },
		{ "F1", VirtualKeys::F1 },
		{ "F2", VirtualKeys::F2 },
		{ "F3", VirtualKeys::F3 },
		{ "F4", VirtualKeys::F4 },
		{ "F5", VirtualKeys::F5 },
		{ "F6", VirtualKeys::F6 },
		{ "F7", VirtualKeys::F7 },
		{ "F8", VirtualKeys::F8 },
		{ "F9", VirtualKeys::F9 },
		{ "F10", VirtualKeys::F10 },
		{ "F11", VirtualKeys::F11 },
		{ "F12", VirtualKeys::F12 },
		{ "F13", VirtualKeys::F13 },
		{ "F14", VirtualKeys::F14 },
		{ "F15", VirtualKeys::F15 },
		{ "F16", VirtualKeys::F16 },
		{ "F17", VirtualKeys::F17 },
		{ "F18", VirtualKeys::F18 },
		{ "F19", VirtualKeys::F19 },
		{ "F20", VirtualKeys::F20 },
		{ "F21", VirtualKeys::F21 },
		{ "F22", VirtualKeys::F22 },
		{ "F23", VirtualKeys::F23 },
		{ "F24", VirtualKeys::F24 },
		{ "NumLock", VirtualKeys::NumLock },
		{ "Scroll", VirtualKeys::Scroll },
		{ "LeftShift", VirtualKeys::LeftShift },
		{ "RightShift", VirtualKeys::RightShift },
		{ "LeftCtrl", VirtualKeys::LeftCtrl },
		{ "RightCtrl", VirtualKeys::RightCtrl },
		{ "LeftMenu", VirtualKeys::LeftMenu },
		{ "RightMenu", VirtualKeys::RightMenu },
	};

	static VirtualKey fromString(const std::string& key) {
		auto found = Lookup.find(key);
		if (found != Lookup.end()) {
			return found->second;
		}

		return VirtualKeys::Unknown;
	}

	static const std::string* toString(VirtualKey idx) {
		for (auto& key : Lookup) {
			if (key.second == idx) {
				return &key.first;
			}
		}

		return nullptr;
	}
}

typedef VirtualKeys::VirtualKey VirtualKey;
