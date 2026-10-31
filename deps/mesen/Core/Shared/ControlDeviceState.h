#pragma once
#include "pch.h"
#include <cstring>
#include "Shared/SettingTypes.h"

struct ControlDeviceState
{
	// RetroPlug: the largest state any read or write can ask for - the byte holding bit 255, plus the two 4-byte
	// coordinate fields a pointing device adds (HasCoordinates()). A state is grown to it in one step (EnsureCapacity),
	// and every place that keeps a state between calls reserves it before copying one in, so a state is never what grows
	// a buffer inside a block.
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