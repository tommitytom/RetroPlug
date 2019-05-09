#pragma once

#include "RingBuffer.h"
#include "Types.h"
#include <string>

struct SramCommand {
	bool save;
	std::string path;
};

struct SettingCommand {
	std::string setting;
	std::string value;
};

class MessageBus {
public:
	// Inputs (probably switch these out for IPlugQueue)
	RingBuffer<ButtonEvent> buttons;
	RingBuffer<LinkEvent> link;

	// Outputs
	RingBuffer<float> audio;
	RingBuffer<char> video;

	MessageBus() {}

	MessageBus(size_t inputBufferSize, size_t audioBufferSize, size_t videoBufferSize) :
		buttons(inputBufferSize), 
		link(inputBufferSize), 
		audio(audioBufferSize), 
		video(videoBufferSize)
	{}
};
