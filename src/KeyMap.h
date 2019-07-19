#pragma once

#include <map>
#include "Types.h"
#include "Keys.h"
#include "Buttons.h"
#include "ConfigLoader.h"
#include "util/xstring.h"
#include "platform/Logger.h"
#include "platform/Path.h"

#include "rapidjson/document.h"

//#define LOG_KEYBOARD_INPUT

class KeyMap {
private:
	std::map<VirtualKey, ButtonType> _keyMap;

public:
	void load(const rapidjson::Value& config) {
		for (const auto& button : config.GetObject()) {
			auto buttonFound = ButtonTypes::fromString(button.name.GetString());
			if (buttonFound != ButtonTypes::MAX) {
				auto keyFound = VirtualKeys::fromString(button.value.GetString());
				if (keyFound != VirtualKeys::Unknown) {
					_keyMap[keyFound] = buttonFound;
				} else {
					std::cout << "Key type '" << button.value.GetString() << "' unknown" << std::endl;
				}
			} else {
				std::cout << "Button type '" << button.name.GetString() << "' unknown" << std::endl;
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
