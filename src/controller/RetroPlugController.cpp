#include "RetroPlugController.h"

RetroPlugBinder::RetroPlugBinder(RetroPlug* model): _model(model), _listener(&_lua) {
	memset(_padButtons, 0, sizeof(_padButtons));
	_padManager = new gainput::InputManager();
	_padId = _padManager->CreateDevice<gainput::InputDevicePad>();

	const std::string path = "../src/scripts";
	_lua.init(_model, path);
	_scriptWatcher.addWatch(path, &_listener, true);
}

RetroPlugBinder::~RetroPlugBinder() {
	delete _padManager;
}

namespace AxisButtons {
	enum AxisButton {
		LeftStickLeft = 0,
		LeftStickRight = 1,
		LeftStickDown = 2,
		LeftStickUp = 3,
		RightStickLeft = 4,
		RightStickRight = 5,
		RightStickDown = 6,
		RightStickUp = 7,
		COUNT
	};
}
using AxisButtons::AxisButton;

const float AXIS_BUTTON_THRESHOLD = 0.5f;

void RetroPlugBinder::update(float delta) {
	_padManager->Update();

	for (int i = 0; i < AxisButtons::COUNT / 2; ++i) {
		float val = _padManager->GetDevice(_padId)->GetFloat(i);
		int l = i * 2;
		int r = i * 2 + 1;

		if (val < -AXIS_BUTTON_THRESHOLD) {			
			if (_padButtons[l] == false) {
				if (_padButtons[r] == true) {
					_padButtons[r] = false;
					onPadButton(r, false);
				}
				
				_padButtons[l] = true;
				onPadButton(l, true);
			}
		} else if (val > AXIS_BUTTON_THRESHOLD) {
			if (_padButtons[r] == false) {
				if (_padButtons[l] == true) {
					_padButtons[l] = false;
					onPadButton(l, false);
				}

				_padButtons[r] = true;
				onPadButton(r, true);
			}
		} else {
			if (_padButtons[l] == true) {
				_padButtons[l] = false;
				onPadButton(l, false);
			}

			if (_padButtons[r] == true) {
				_padButtons[r] = false;
				onPadButton(r, false);
			}
		}
	}

	for (int i = 0; i < gainput::PadButtonCount_; ++i) {
		int idx = gainput::PadButtonStart + i;
		bool down = _padManager->GetDevice(_padId)->GetBool(idx);
		if (_padButtons[idx] != down) {
			_padButtons[idx] = down;
			onPadButton(idx, down);
		}
	}
}

bool RetroPlugBinder::onKey(const iplug::igraphics::IKeyPress& key, bool down) {
	return _lua.onKey(key, down);
}

void RetroPlugBinder::onPadButton(int button, bool down) {
	std::cout << button << "        " << down << std::endl;
	_lua.onPadButton(button, down);
}
