
#ifndef GAINPUTINPUTDEVICEMOUSEMAC_H_
#define GAINPUTINPUTDEVICEMOUSEMAC_H_

#include "GainputInputDeviceMouseImpl.h"

namespace gainput
{

class InputDeviceMouseImplMac : public InputDeviceMouseImpl
{
public:
	InputDeviceMouseImplMac(InputManager& manager, InputDevice& device, InputState& state, InputState& previousState);
	~InputDeviceMouseImplMac();

	InputDevice::DeviceVariant GetVariant() const
	{
		return InputDevice::DV_STANDARD;
	}

	void Update(InputDeltaState* delta);

	InputManager& manager_;
	InputDevice& device_;
	InputState* state_;
	InputState* previousState_;
	InputState nextState_;
	InputDeltaState* delta_;

	void* eventTap_;
};

}

#endif

