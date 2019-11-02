#pragma once

#include <gainput/gainput.h>

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

static int buttonToId(const std::string& name) {
	if (name == "Start") return gainput::PadButtonStart;
	if (name == "Select") return gainput::PadButtonSelect;
	if (name == "Left") return gainput::PadButtonLeft;
	if (name == "Right") return gainput::PadButtonRight;
	if (name == "Up") return gainput::PadButtonUp;
	if (name == "Down") return gainput::PadButtonDown;
	if (name == "A") return gainput::PadButtonA;
	if (name == "B") return gainput::PadButtonB;
	if (name == "X") return gainput::PadButtonX;
	if (name == "Y") return gainput::PadButtonY;
	if (name == "L1") return gainput::PadButtonL1;
	if (name == "R1") return gainput::PadButtonR1;
	if (name == "L2") return gainput::PadButtonL2;
	if (name == "R2") return gainput::PadButtonR2;
	if (name == "L3") return gainput::PadButtonL3;
	if (name == "R3") return gainput::PadButtonR3;
	if (name == "Home") return gainput::PadButtonHome;
	if (name == "Button17") return gainput::PadButton17;
	if (name == "Button18") return gainput::PadButton18;
	if (name == "Button19") return gainput::PadButton19;
	if (name == "Button20") return gainput::PadButton20;
	if (name == "Button21") return gainput::PadButton21;
	if (name == "Button22") return gainput::PadButton22;
	if (name == "Button23") return gainput::PadButton23;
	if (name == "Button24") return gainput::PadButton24;
	if (name == "Button25") return gainput::PadButton25;
	if (name == "Button26") return gainput::PadButton26;
	if (name == "Button27") return gainput::PadButton27;
	if (name == "Button28") return gainput::PadButton28;
	if (name == "Button29") return gainput::PadButton29;
	if (name == "Button30") return gainput::PadButton30;
	if (name == "Button31") return gainput::PadButton31;
	return -1;
}

template <typename KeyType>
class KeyMap {
private:
	std::map<KeyType, ButtonType> _keyMap;

public:
	void loadKeys(const rapidjson::Value& config) {
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

	void loadPad(const rapidjson::Value& config) {
		for (const auto& button : config.GetObject()) {
			auto buttonFound = ButtonTypes::fromString(button.name.GetString());
			if (buttonFound != ButtonTypes::MAX) {
				int keyFound = buttonToId(button.value.GetString());
				
				if (keyFound != -1) {
					_keyMap[keyFound] = buttonFound;
				} else {
					std::cout << "Pad button type '" << button.value.GetString() << "' unknown" << std::endl;
				}
			} else {
				std::cout << "Button type '" << button.name.GetString() << "' unknown" << std::endl;
			}
		}
	}

	ButtonType getControllerButton(KeyType key) const {
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
