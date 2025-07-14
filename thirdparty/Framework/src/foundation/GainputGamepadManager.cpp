#include "GainputGamepadManager.h"

#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace fw {
	

	GainputGamepadManager::GainputGamepadManager(): _myDeviceButtonListener(_padManager), _inputMap(_padManager, "testmap") {
		_padId = _padManager.CreateDevice<gainput::InputDevicePad>();

		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Start, _padId, gainput::PadButtonStart);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Select, _padId, gainput::PadButtonSelect);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Left, _padId, gainput::PadButtonLeft);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Right, _padId, gainput::PadButtonRight);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Up, _padId, gainput::PadButtonUp);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Down, _padId, gainput::PadButtonDown);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::A, _padId, gainput::PadButtonA);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::B, _padId, gainput::PadButtonB);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::X, _padId, gainput::PadButtonX);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Y, _padId, gainput::PadButtonY);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::L1, _padId, gainput::PadButtonL1);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::R1, _padId, gainput::PadButtonR1);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::L2, _padId, gainput::PadButtonL2);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::R2, _padId, gainput::PadButtonR2);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::L3, _padId, gainput::PadButtonL3);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::R3, _padId, gainput::PadButtonR3);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Home, _padId, gainput::PadButtonHome);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button17, _padId, gainput::PadButton17);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button18, _padId, gainput::PadButton18);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button19, _padId, gainput::PadButton19);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button20, _padId, gainput::PadButton20);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button21, _padId, gainput::PadButton21);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button22, _padId, gainput::PadButton22);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button23, _padId, gainput::PadButton23);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button24, _padId, gainput::PadButton24);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button25, _padId, gainput::PadButton25);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button26, _padId, gainput::PadButton26);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button27, _padId, gainput::PadButton27);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button28, _padId, gainput::PadButton28);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button29, _padId, gainput::PadButton29);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button30, _padId, gainput::PadButton30);
		_inputMap.MapBool((gainput::UserButtonId)fw::PadButtonType::Button31, _padId, gainput::PadButton31);
	}

	fw::PadButtonType convertButtonType(int button) {
		return static_cast<fw::PadButtonType>(button);
	}

	void GainputGamepadManager::update() {
		_padManager.Update();

		if (!_buttonCallback) {
			return;
		}

		for (int i = (int)fw::PadButtonType::Start; i < (int)fw::PadButtonType::COUNT; ++i) {
			const bool down = _inputMap.GetBool((gainput::UserButtonId)i);
			fw::PadButtonType button = convertButtonType(i);

			if (_padButtons[i] != down) {
				_padButtons[i] = down;
				_buttonCallback(button, down);
			}
		}

		if (_axisButtonThreshold <= 0.0f) {
			return;
		}

		for (int i = 0; i < (int)AxisButton::COUNT / 2; ++i) {
			float val = _padManager.GetDevice(_padId)->GetFloat(i);
			int l = i * 2;
			int r = i * 2 + 1;

			if (val < -_axisButtonThreshold) {
				if (_padButtons[l] == false) {
					if (_padButtons[r] == true) {
						_padButtons[r] = false;
						_buttonCallback(convertButtonType(r), false);
					}

					_padButtons[l] = true;
					_buttonCallback(convertButtonType(l), true);
				}
			} else if (val > _axisButtonThreshold) {
				if (_padButtons[r] == false) {
					if (_padButtons[l] == true) {
						_padButtons[l] = false;
						_buttonCallback(convertButtonType(l), false);
					}

					_padButtons[r] = true;
					_buttonCallback(convertButtonType(r), true);
				}
			} else {
				if (_padButtons[l] == true) {
					_padButtons[l] = false;
					_buttonCallback(convertButtonType(l), false);
				}

				if (_padButtons[r] == true) {
					_padButtons[r] = false;
					_buttonCallback(convertButtonType(r), false);
				}
			}
		}
	}
}
