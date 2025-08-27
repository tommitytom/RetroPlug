#pragma once

#include <functional>

#include <gainput/gainput.h>

#include "GamepadManager.h"
#include "foundation/Input.h"

namespace fw {
	enum class AxisButton {
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

	class MyDeviceButtonListener : public gainput::InputListener
	{
	public:
		MyDeviceButtonListener(gainput::InputManager& manager) : manager(manager) {}

		bool OnDeviceButtonBool(gainput::DeviceId deviceId, gainput::DeviceButtonId deviceButton, bool oldValue, bool newValue) {
			const gainput::InputDevice* device = manager.GetDevice(deviceId);
			char buttonName[64];
			device->GetButtonName(deviceButton, buttonName, 64);
			printf("Device %d (%s%d) bool button (%d/%s) changed: %d -> %d\n", deviceId, device->GetTypeName(), device->GetIndex(), deviceButton, buttonName, oldValue, newValue);
			return true;
		}

		bool OnDeviceButtonFloat(gainput::DeviceId deviceId, gainput::DeviceButtonId deviceButton, float oldValue, float newValue) {
			const gainput::InputDevice* device = manager.GetDevice(deviceId);
			char buttonName[64];
			device->GetButtonName(deviceButton, buttonName, 64);
			printf("Device %d (%s%d) float button (%d/%s) changed: %f -> %f\n", deviceId, device->GetTypeName(), device->GetIndex(), deviceButton, buttonName, oldValue, newValue);
			return true;
		}

	private:
		gainput::InputManager& manager;
	};

	class GainputGamepadManager : public fw::GamepadManager {
	public:
		using ButtonCallback = std::function<void(fw::PadButtonType, bool)>;

	private:
		gainput::InputManager _padManager;
		gainput::DeviceId _padId;
		bool _padButtons[(int)fw::PadButtonType::COUNT] = { false };
		float _axisButtonThreshold = 0.0f;
		ButtonCallback _buttonCallback;

		MyDeviceButtonListener _myDeviceButtonListener;
		gainput::InputMap _inputMap;

	public:
		GainputGamepadManager();
		~GainputGamepadManager() = default;

		void update() override;

		void setAxisButtonThreshold(float threshold) override {
			_axisButtonThreshold = threshold;
		}

		void setCallback(ButtonCallback&& cb) {
			_buttonCallback = std::move(cb);
		}
	};
}
