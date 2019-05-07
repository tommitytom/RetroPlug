#pragma once

enum class ButtonType {
	GB_KEY_RIGHT,
	GB_KEY_LEFT,
	GB_KEY_UP,
	GB_KEY_DOWN,
	GB_KEY_A,
	GB_KEY_B,
	GB_KEY_SELECT,
	GB_KEY_START,
	GB_KEY_MAX
};

struct ButtonEvent {
	size_t offset;
	size_t id;
	bool down;
};

struct LinkEvent {
	size_t offset;
	unsigned char byte;
};
