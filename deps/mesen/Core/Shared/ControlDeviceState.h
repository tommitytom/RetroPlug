#pragma once
#include "pch.h"
#include <cstring>
#include "Shared/SettingTypes.h"

struct ControlDeviceState
{
	// RetroPlug: the largest a single device's bit/coordinate state can be grown to - the byte holding bit 255, plus
	// the 32 bytes EnsureCapacity adds for a pointing device that has coordinates (4 of them the coordinates, the rest
	// margin). A state is grown to it in one step (EnsureCapacity), and every place that keeps a device's state between
	// calls reserves room before copying one in, so such a state never grows a buffer inside a block. A controller hub's
	// composite state (four ports) and a barcode reader's text are longer and may; neither is reachable here.
	static constexpr uint32_t MaxBytes = 255 / 8 + 1 + 32;

	vector<uint8_t> State;

	bool operator!=(ControlDeviceState &other)
	{
		return State.size() != other.State.size() || memcmp(State.data(), other.State.data(), State.size()) != 0;
	}
};

struct ControllerData
{
	ControllerType Type;
	ControlDeviceState State;
	uint8_t Port;
};