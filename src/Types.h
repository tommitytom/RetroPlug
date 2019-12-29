#pragma once

#include "Constants.h"

struct ButtonEvent {
	size_t id;
	bool down;
};

struct LinkEvent {
	size_t offset;
	unsigned char byte;
};

struct Dimension2 {
	int w = 0;
	int h = 0;
};

using InstanceIndex = int;
const InstanceIndex NO_ACTIVE_INSTANCE = -1;
