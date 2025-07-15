#pragma once

#include <assert.h>
#include <array>
#include "foundation/Input.h"

namespace fw {
	struct StreamButtonPress {
		int button;
		bool down;
		double duration = 0; // in ms
	};

	class ButtonStreamWriter {
	private:
		std::vector<StreamButtonPress>& _stream;
		std::array<bool, static_cast<int>(ButtonType::MAX)> _state = { false };
		double _defaultDelay = 50.0;

	public:
		ButtonStreamWriter(std::vector<StreamButtonPress>& stream): _stream(stream) {}
		~ButtonStreamWriter() = default;

		ButtonStreamWriter& press(int button) {
			hold(button);
			release(button);
			return *this;
		}

		ButtonStreamWriter& hold(int button) { return holdDuration(button, -1); }

		ButtonStreamWriter& holdDuration(int button, double postDelay) {
			if (!_state[button]) {
				if (postDelay < 0) {
					postDelay = _defaultDelay;
				}

				_stream.push_back(StreamButtonPress{ button, true, postDelay });
				_state[button] = true;
			}

			return *this;
		}

		ButtonStreamWriter& release(int button) { return releaseDuration(button, -1); }

		ButtonStreamWriter& releaseDuration(int button, double postDelay) {
			if (_state[button]) {
				if (postDelay < 0) {
					postDelay = _defaultDelay;
				}

				_stream.push_back(StreamButtonPress{ button, false, postDelay });
				_state[button] = false;
			}

			return *this;
		}

		ButtonStreamWriter& delay(double d) {
			if (_stream.size() > 0) {
				_stream.back().duration += d;
			}

			return *this;
		}

		ButtonStreamWriter& releaseAll() { return releaseAllDuration(-1); }

		ButtonStreamWriter& releaseAllDuration(double postDelay) {
			for (int i = 0; i < _state.size(); ++i) {
				if (_state[i]) {
					releaseDuration(i, 0);
				}
			}

			delay(postDelay < 0 ? _defaultDelay : postDelay);

			return *this;
		}

		void clear() {
			_stream.clear();
		}

		size_t getCount() const { return _stream.size(); }

		void setDefaultDelay(double delay) {
			_defaultDelay = delay;
		}

		double getDefaultDelay() const {
			return _defaultDelay;
		}

		const std::vector<fw::StreamButtonPress>& data() const { return _stream; }

		std::vector<fw::StreamButtonPress>& data() { return _stream; }
	};
}
