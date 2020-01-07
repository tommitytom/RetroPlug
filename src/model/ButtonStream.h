#pragma once

#include "Buttons.h"
#include <assert.h>

struct StreamButtonPress {
	int button;
	bool down;
	double duration; // in ms
};

template <const int TotalPressCount>
struct ButtonStream {
	InstanceIndex idx;
	std::array<StreamButtonPress, TotalPressCount> presses;
	size_t pressCount = 0;
};

template <const int ButtonCount, const int TotalPressCount>
class ButtonStreamWriter {
private:
	ButtonStream<TotalPressCount> _stream;
	bool _state[ButtonCount] = { false };
	double _defaultDelay = 50.0;

public:
	ButtonStreamWriter& press(int button) {
		hold(button);
		release(button);
		return *this;
	}

	ButtonStreamWriter& hold(int button) { return holdDuration(button, -1); }

	ButtonStreamWriter& holdDuration(int button, double postDelay) {
		assert(_stream.pressCount < TotalPressCount);
		if (!_state[button]) {
			if (postDelay < 0) {
				postDelay = _defaultDelay;
			}

			_stream.presses[_stream.pressCount++] = StreamButtonPress { button, true, postDelay };
			_state[button] = true;
		}

		return *this;
	}

	ButtonStreamWriter& release(int button) { return releaseDuration(button, -1); }

	ButtonStreamWriter& releaseDuration(int button, double postDelay) {
		assert(_stream.pressCount < TotalPressCount);
		if (_state[button]) {
			if (postDelay < 0) {
				postDelay = _defaultDelay;
			}

			_stream.presses[_stream.pressCount++] = StreamButtonPress{ button, false, postDelay };
			_state[button] = false;
		}

		return *this;
	}

	ButtonStreamWriter& delay(double d) {
		if (_stream.pressCount > 0) {
			_stream.presses[_stream.pressCount - 1].duration += d;
		}

		return *this;
	}

	ButtonStreamWriter& releaseAll() { return releaseAllDuration(-1); }

	ButtonStreamWriter& releaseAllDuration(double postDelay) {
		for (int i = 0; i < ButtonCount; ++i) {
			if (_state[i]) {
				releaseDuration(i, 0);
			}
		}

		delay(postDelay < 0 ? _defaultDelay : postDelay);
		
		return *this;
	}

	void clear() {
		_stream.pressCount = 0;
	}

	size_t getCount() const { return _stream.pressCount; }

	void setDefaultDelay(double delay) {
		_defaultDelay = delay;
	}

	double getDefaultDelay() const {
		return _defaultDelay;
	}

	const ButtonStream<TotalPressCount>& data() const { return _stream; }

	ButtonStream<TotalPressCount>& data() { return _stream; }

	void setIndex(InstanceIndex idx) {
		_stream.idx = idx;
	}
};

using GameboyButtonStream = ButtonStreamWriter<ButtonType::MAX, 32>;
