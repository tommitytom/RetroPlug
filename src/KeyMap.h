#pragma once

#include <map>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "tao/json.hpp"
#include "util/String.h"
#include "platform/Logger.h"
#include "Types.h"
#include "platform/Path.h"

const std::map<std::string, ButtonType> ButtonLookup = {
	{ "Left", ButtonType::GB_KEY_LEFT },
	{ "Up", ButtonType::GB_KEY_UP },
	{ "Right", ButtonType::GB_KEY_RIGHT },
	{ "Down", ButtonType::GB_KEY_DOWN },
	{ "A", ButtonType::GB_KEY_A },
	{ "B", ButtonType::GB_KEY_B },
	{ "Start", ButtonType::GB_KEY_START },
	{ "Select", ButtonType::GB_KEY_SELECT }
};

const std::map<std::string, VirtualKeys> KeyLookup = {
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

const std::string DEFAULT_CONFIG = "{\"Up\":\"UpArrow\",\"Down\":\"DownArrow\",\"Left\":\"LeftArrow\",\"Right\":\"RightArrow\",\"A\":\"Z\",\"B\":\"X\",\"Start\":\"Enter\",\"Select\":\"Ctrl\"}";

//#define LOG_KEYBOARD_INPUT

class KeyMap {
private:
	std::map<int, int> _keyMap;

public:
	void load() {
		std::string contentPath = getContentPath();
		if (!std::filesystem::exists(contentPath)) {
			std::filesystem::create_directory(contentPath);
		}

		std::string buttonPath = contentPath + "\\buttons.json";
		if (!std::filesystem::exists(buttonPath)) {
			std::ofstream configOut(buttonPath);
			auto defaultConfig = tao::json::from_string(DEFAULT_CONFIG);
			tao::json::to_stream(configOut, defaultConfig, 2);
		}

		tao::json::value buttonConfig;
		if (std::filesystem::exists(buttonPath)) {
			buttonConfig = tao::json::parse_file(buttonPath);
			if (!buttonConfig.is_object()) {
				buttonConfig = tao::json::from_string(DEFAULT_CONFIG);
			}
		} else {
			buttonConfig = tao::json::from_string(DEFAULT_CONFIG);
		}

		for (const auto& button : buttonConfig.get_object()) {
			auto buttonFound = ButtonLookup.find(button.first);
			if (buttonFound == ButtonLookup.end()) {
				std::cout << "Button type '" << button.first << "' unknown" << std::endl;
			} else {
				auto keyFound = KeyLookup.find(button.second.get_string());
				if (keyFound == KeyLookup.end()) {
					std::cout << "Key type '" << button.second.get_string() << "' unknown" << std::endl;
				} else {
					_keyMap[(int)keyFound->second] = (int)buttonFound->second;
				}
			}
		}
	}

	int getControllerButton(int key) const {
#ifdef LOG_KEYBOARD_INPUT
		const std::string* keyName = getKeyName(key);
		if (keyName) {
			consoleLog("Key pressed: " + *keyName);
		} else {
			consoleLog("Key with index " + std::to_string(key) + " not found");
		}
#endif

		auto found = _keyMap.find(key);
		if (found != _keyMap.end()) {
			return found->second;
		}

		return -1;
	}

	const std::string* getKeyName(int idx) const {
		for (auto& key : KeyLookup) {
			if ((int)key.second == idx) {
				return &key.first;
			}
		}

		return nullptr;
	}
};
