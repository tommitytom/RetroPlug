#pragma once

#include "Types.h"
#include <queue>
#include "libretroplug/MessageBus.h"
#include "platform/Logger.h"
//#include "IPlugQueue.h"

const double CONSECUTIVE_PRESS_DELAY = 50;
const double MODIFIER_PRESS_DELAY = 100;
const double DEFAULT_PRESS_DURATION = 50;

enum ButtonPressType {
	Press,
	Hold,
	Release
};

static std::string buttonPressString(ButtonPressType type) {
	switch (type) {
	case ButtonPressType::Press: return "Press";
	case ButtonPressType::Hold: return "Hold";
	case ButtonPressType::Release: return "Release";
	}

	return "";
}

static std::string buttonString(ButtonType button) {
	switch (button) {
	case ButtonType::GB_KEY_LEFT: return "Left";
	case ButtonType::GB_KEY_UP: return "Up";
	case ButtonType::GB_KEY_RIGHT: return "Right";
	case ButtonType::GB_KEY_DOWN: return "Down";
	case ButtonType::GB_KEY_A: return "A";
	case ButtonType::GB_KEY_B: return "B";
	case ButtonType::GB_KEY_START: return "Start";
	case ButtonType::GB_KEY_SELECT: return "Select";
	}

	return "";
}

struct ButtonPress {
	ButtonType button;
	ButtonPressType type;
	double startTime;
	double duration;
	bool active;
	bool complete;
};

class ButtonQueue {
private:
	bool _state[ButtonType::GB_KEY_MAX] = { false };
	std::vector<ButtonPress> _presses;

public:
	void clear() {
		_presses.clear();
	}

	ButtonQueue& hold(ButtonType button, double delay = 0) {
		addPress(button, ButtonPressType::Hold, delay, 0);
		return *this;
	}

	ButtonQueue& holdModified(ButtonType button, ButtonType modifier, double delay = 0) {
		addPressModified(button, modifier, ButtonPressType::Hold, delay, 0);
		return *this;
	}

	ButtonQueue& release(ButtonType button, double delay = 0) {
		addPress(button, ButtonPressType::Release, delay, 0);
		return *this;
	}

	ButtonQueue& press(ButtonType button, double delay = 0, double duration = DEFAULT_PRESS_DURATION) {
		addPress(button, ButtonPressType::Press, delay, duration);
		return *this;
	}

	ButtonQueue& pressModified(ButtonType button, ButtonType modifier, double delay = 0, double duration = DEFAULT_PRESS_DURATION) {
		addPressModified(button, modifier, ButtonPressType::Press, delay, duration);
		return *this;
	}

	void update(MessageBus* bus, double delta) {
		for (size_t i = 0; i < _presses.size(); i++) {
			ButtonPress& press = _presses[i];
			if (!press.active) {
				// Button press/release is queued but not yet processed
				if (press.startTime <= delta) {
					press.duration -= press.startTime;
					press.startTime = 0;
					press.active = true;

					ButtonEvent ev = { press.button, press.type != ButtonPressType::Release };
					bus->buttons.writeValue(ev);

					press.complete = press.type != ButtonPressType::Press;
					_state[press.button] = ev.down;

					consoleLogLine("Button " + buttonPressString(press.type) + ": " + buttonString(press.button));
				} else {
					press.startTime -= delta;
				}
			} else {
				// Button has been pressed and is awaiting release.  Only ButtonPressType::Press
				// events will end up here
				if (press.duration < delta) {
					bus->buttons.writeValue(ButtonEvent{ press.button, false });
					press.complete = true;
					_state[press.button] = false;

					consoleLogLine("Button Release: " + buttonString(press.button));
				} else {
					press.duration -= delta;
				}
			}
		}

		for (int i = _presses.size() - 1; i >= 0; i--) {
			ButtonPress& press = _presses[i];
			if (press.complete) {
				_presses.erase(_presses.begin() + i);
			}
		}
	}

private:
	void addPress(ButtonType button, ButtonPressType type, double delay, double duration) {
		double startTime = getPressStartTime(delay);
		_presses.push_back(ButtonPress { button, type, startTime, duration, false, false });
	}

	void addPressModified(ButtonType button, ButtonType modifier, ButtonPressType type, double delay, double duration) {
		double startTime = getPressStartTime(delay);
		_presses.push_back(ButtonPress { modifier, ButtonPressType::Press, startTime, duration + MODIFIER_PRESS_DELAY, false, false });
		_presses.push_back(ButtonPress { button, type, delay + MODIFIER_PRESS_DELAY, duration, false, false });
	}

	double getPressStartTime(double delay) const {
		if (!_presses.empty()) {
			const ButtonPress& current = _presses.back();
			double minStartTime = current.startTime + current.duration + CONSECUTIVE_PRESS_DELAY;
			return std::max(delay, minStartTime);
		}

		return delay;
	}
};
