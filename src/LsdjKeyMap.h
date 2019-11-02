#pragma once

#include <map>
#include "IGraphicsStructs.h"

#include "Keys.h"
#include "Types.h"
#include "Buttons.h"
#include "ButtonQueue.h"
#include "KeyMap.h"
#include "libretroplug/MessageBus.h"

#include "rapidjson/document.h"

using namespace iplug;
using namespace igraphics;

enum State {
	None,
	Selecting
};

enum class LsdjActionType {
	UpTenRows,
	DownTenRows,
	ScreenUp,
	ScreenDown,
	ScreenLeft,
	ScreenRight,
	CancelSelection,
	Delete
};

const std::map<std::string, LsdjActionType> ActionLookup = {
	{ "UpTenRows", LsdjActionType::UpTenRows },
	{ "DownTenRows", LsdjActionType::DownTenRows },
	{ "ScreenUp", LsdjActionType::ScreenUp },
	{ "ScreenDown", LsdjActionType::ScreenDown },
	{ "ScreenLeft", LsdjActionType::ScreenLeft },
	{ "ScreenRight", LsdjActionType::ScreenRight },
	{ "CancelSelection", LsdjActionType::CancelSelection },
	{ "Delete", LsdjActionType::Delete }
};

class LsdjKeyMap {
private:
	State _state = State::None;
	ButtonQueue _presses;
	KeyMap<VirtualKey>* _gbKeyMap;
	std::map<VirtualKey, LsdjActionType> _actionMap;

public:
	void load(KeyMap<VirtualKey>& keyMap, const rapidjson::Value& config) {
		_gbKeyMap = &keyMap;

		for (const auto& action : config.GetObject()) {
			auto actionFound = ActionLookup.find(action.name.GetString());
			if (actionFound == ActionLookup.end()) {
				std::cout << "Action type '" << action.name.GetString() << "' unknown" << std::endl;
			} else {
				auto keyFound = VirtualKeys::fromString(action.value.GetString());
				if (keyFound == VirtualKeys::Unknown) {
					std::cout << "Key type '" << action.value.GetString() << "' unknown" << std::endl;
				} else {
					_actionMap[keyFound] = actionFound->second;
				}
			}
		}
	}

	void clear() {
		_presses.clear();
	}

	void update(MessageBus* bus, double delta) {
		_presses.update(bus, delta);
	}

	bool onKey(const IKeyPress& key, bool down) {
		VirtualKey vk = (VirtualKey)key.VK;
		ButtonType b = _gbKeyMap->getControllerButton(vk);

		if (down && key.C) {
			switch (vk) {
			case VirtualKeys::X: cut(); return true;
			case VirtualKeys::C: copy(); return true;
			case VirtualKeys::V: paste(); return true;
			}
		}

		switch (b) {
		case ButtonTypes::Left:
		case ButtonTypes::Right:
		case ButtonTypes::Up:
		case ButtonTypes::Down:
			if (down) {
				if (_state == State::None && key.S) {
					beginSelect().hold(b, 200);
				} else if (_state == State::Selecting && !key.S) {
					endSelect().hold(b, 200);
				} else if (_state == State::None && key.C) {
					_presses.holdModified(b, ButtonTypes::Select);
				} else {
					_presses.hold(b);
				}
			} else {
				_presses.release(b);
			}

			return true;
		case ButtonTypes::MAX:
			// Key press does not relate to a button but may still be used
			if (down) {
				auto found = _actionMap.find(vk);
				if (found != _actionMap.end()) {
					if (_state == State::None) {
						switch (found->second) {
						case LsdjActionType::ScreenRight: screenMove(ButtonTypes::Right); break;
						case LsdjActionType::ScreenLeft: screenMove(ButtonTypes::Left); break;
						case LsdjActionType::ScreenUp: screenMove(ButtonTypes::Up); break;
						case LsdjActionType::ScreenDown: screenMove(ButtonTypes::Down); break;
						case LsdjActionType::UpTenRows: rowJump(ButtonTypes::Up); break;
						case LsdjActionType::DownTenRows: rowJump(ButtonTypes::Down); break;
						case LsdjActionType::Delete: deleteSelection(); break;
						}
					} else {
						switch (found->second) {
						case LsdjActionType::CancelSelection: endSelect(); break;
						}
					}

					return true;
				}
			}

			return false;
		default:
			if (_state == State::None) {
				if (down) {
					_presses.hold(b);
				} else {
					_presses.release(b);
				}
			}

			return true;
		}

		return false;
	}

private:
	ButtonQueue& beginSelect() {
		consoleLogLine("beginSelect");
		_state = State::Selecting;
		return _presses.pressModified(ButtonTypes::B, ButtonTypes::Select);
	}

	ButtonQueue& endSelect() {
		consoleLogLine("endSelect");
		_state = State::None;
		// This is technically a copy operation, but seems to be the only non destructive
		// way of ending select mode
		return _presses.press(ButtonTypes::B);
	}

	ButtonQueue& copy() {
		consoleLogLine("copy");
		int delay = 0;
		if (_state != State::Selecting) {
			beginSelect();
			delay = 150;
		}

		_state = State::None;
		return _presses.press(ButtonTypes::B, delay);
	}

	ButtonQueue& cut() {
		consoleLogLine("cut");
		int delay = 0;
		if (_state != State::Selecting) {
			beginSelect();
			delay = 150;
		}

		_state = State::None;
		return _presses.pressModified(ButtonTypes::A, ButtonTypes::Select, delay);
	}

	ButtonQueue& paste() {
		consoleLogLine("paste");
		assert(_state == State::None);
		return _presses.pressModified(ButtonTypes::A, ButtonTypes::Select);
	}

	ButtonQueue& screenMove(ButtonType button) {
		consoleLogLine("screenMove");
		assert(_state == State::None);
		return _presses.pressModified(button, ButtonTypes::Select);
	}

	ButtonQueue& rowJump(ButtonType button) {
		consoleLogLine("rowJump");
		assert(_state == State::None);
		return _presses.pressModified(button, ButtonTypes::B);
	}

	ButtonQueue& deleteSelection() {
		consoleLogLine("deleteSelection");
		return _presses.pressModified(ButtonTypes::A, ButtonTypes::B);
	}
};
