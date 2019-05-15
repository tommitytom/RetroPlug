#pragma once

#include "IGraphicsStructs.h"
#include "Types.h"
#include "ButtonQueue.h"
#include "libretroplug/MessageBus.h"
#include <windows.h>
#include <map>

enum State {
	None,
	Selecting
};

class LsdjKeyMap {
private:
	State _state = State::None;
	ButtonQueue _presses;
	std::map<VirtualKeys, ButtonType> _keyMap;

public:
	LsdjKeyMap() {
		_keyMap[VirtualKeys::LeftArrow] = ButtonType::GB_KEY_LEFT;
		_keyMap[VirtualKeys::RightArrow] = ButtonType::GB_KEY_RIGHT;
		_keyMap[VirtualKeys::UpArrow] = ButtonType::GB_KEY_UP;
		_keyMap[VirtualKeys::DownArrow] = ButtonType::GB_KEY_DOWN;

		_keyMap[VirtualKeys::A] = ButtonType::GB_KEY_A;
		_keyMap[VirtualKeys::S] = ButtonType::GB_KEY_B;

		_keyMap[VirtualKeys::Space] = ButtonType::GB_KEY_START;
		_keyMap[VirtualKeys::Alt] = ButtonType::GB_KEY_SELECT;
	}

	void clear() {
		_presses.clear();
	}

	void update(MessageBus* bus, double delta) {
		_presses.update(bus, delta);
	}

	bool onKey(const IKeyPress& key, bool down) {
		VirtualKeys vk = (VirtualKeys)key.VK;
		ButtonType b = ButtonType::GB_KEY_MAX;
		const auto& found = _keyMap.find(vk);
		if (found != _keyMap.end()) {
			b = found->second;
		}

		switch (b) {
		case ButtonType::GB_KEY_LEFT:
		case ButtonType::GB_KEY_RIGHT:
		case ButtonType::GB_KEY_UP:
		case ButtonType::GB_KEY_DOWN:
			if (down) {
				if (_state == State::None && key.S) {
					beginSelect().hold(b, 200);
				} else if (_state == State::Selecting && !key.S) {
					endSelect().hold(b, 200);
				} else if (_state == State::None && key.C) {
					_presses.holdModified(b, ButtonType::GB_KEY_SELECT);
				} else {
					_presses.hold(b);
				}
			} else {
				_presses.release(b);
			}

			return true;
		case ButtonType::GB_KEY_MAX:
			// Key press does not relate to a button but may still be used
			if (down && key.C) {
				switch (vk) {
				case VirtualKeys::X: cut(); break;
				case VirtualKeys::C: copy(); break;
				case VirtualKeys::V: paste(); break;
				}
			} else if (down && _state == State::None) {
				switch (vk) {
				case VirtualKeys::Enter: nextPage(); break;
				case VirtualKeys::Backspace: prevPage(); break;
				case VirtualKeys::Esc: endSelect(); break;
				case VirtualKeys::PageUp: pageUp(); break;
				case VirtualKeys::PageDown: pageDown(); break;
				}
			}

			return false;
		default:
			// In selection mode, all button presses are ignored unless 
			// they are directional
			if (_state == State::None) {
				if (down) {
					_presses.hold(b);
				} else {
					_presses.release(b);
				}
				
				return true;
			}
		}

		return false;
	}

private:
	ButtonQueue& nextPage() {
		consoleLogLine("nextPage");
		assert(_state == State::None);
		return _presses.pressModified(ButtonType::GB_KEY_RIGHT, ButtonType::GB_KEY_SELECT);
	}

	ButtonQueue& prevPage() {
		consoleLogLine("prevPage");
		assert(_state == State::None);
		return _presses.pressModified(ButtonType::GB_KEY_LEFT, ButtonType::GB_KEY_SELECT);
	}

	ButtonQueue& beginSelect() {
		consoleLogLine("beginSelect");
		_state = State::Selecting;
		return _presses.pressModified(ButtonType::GB_KEY_B, ButtonType::GB_KEY_SELECT);
	}

	ButtonQueue& endSelect() {
		consoleLogLine("endSelect");
		_state = State::None;
		// This is technically a copy operation, but seems to be the only non destructive
		// way of ending select mode
		return _presses.press(ButtonType::GB_KEY_B);
	}

	ButtonQueue& copy() {
		consoleLogLine("copy");
		if (_state != State::Selecting) {
			beginSelect();
		}

		_state = State::None;
		return _presses.press(ButtonType::GB_KEY_B);
	}

	ButtonQueue& cut() {
		consoleLogLine("cut");
		if (_state != State::Selecting) {
			beginSelect();
		}

		_state = State::None;
		return _presses.pressModified(ButtonType::GB_KEY_A, ButtonType::GB_KEY_SELECT);
	}

	ButtonQueue& paste() {
		consoleLogLine("paste");
		assert(_state == State::None);
		return _presses.pressModified(ButtonType::GB_KEY_A, ButtonType::GB_KEY_SELECT);
	}

	ButtonQueue& pageUp() {
		consoleLogLine("pageUp");
		assert(_state == State::None);
		return _presses.pressModified(ButtonType::GB_KEY_UP, ButtonType::GB_KEY_B);
	}

	ButtonQueue& pageDown() {
		consoleLogLine("pageDown");
		assert(_state == State::None);
		return _presses.pressModified(ButtonType::GB_KEY_DOWN, ButtonType::GB_KEY_B);
	}
};
