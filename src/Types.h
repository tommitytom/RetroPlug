#pragma once

struct ButtonEvent {
	size_t id;
	bool down;
};

struct LinkEvent {
	size_t offset;
	unsigned char byte;
};
