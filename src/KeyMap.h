#pragma once

#include <map>
#include <tao/json.hpp>
#include "Types.h"
#include "Keys.h"
#include "Buttons.h"
#include "ConfigLoader.h"
#include "util/String.h"
#include "platform/Logger.h"
#include "platform/Path.h"

//#define LOG_KEYBOARD_INPUT

class KeyMap {
private:
	std::map<VirtualKey, ButtonType> _keyMap;

public:
	void load(const tao::json::value& config) {
		for (const auto& button : config.get_object()) {
			auto buttonFound = ButtonTypes::fromString(button.first);
			if (buttonFound != ButtonTypes::MAX) {
				auto keyFound = VirtualKeys::fromString(button.second.get_string());
				if (keyFound != VirtualKeys::Unknown) {
					_keyMap[keyFound] = buttonFound;
				} else {
					std::cout << "Key type '" << button.second.get_string() << "' unknown" << std::endl;
				}
			} else {
				std::cout << "Button type '" << button.first << "' unknown" << std::endl;
			}
		}	
	}

	ButtonType getControllerButton(VirtualKey key) const {
#ifdef LOG_KEYBOARD_INPUT
		const std::string* keyName = VirtualKeys::toString(key);
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

		return ButtonType::MAX;
	}
};
