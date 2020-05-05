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

using SystemIndex = int;
const SystemIndex NO_ACTIVE_SYSTEM = -1;
