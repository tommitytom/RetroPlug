#pragma once

#include "RingBuffer.h"
#include "Types.h"

class MessageBus {
public:
	// Inputs
	RingBuffer<ButtonEvent> buttons;
	RingBuffer<LinkEvent> link;

	// Outputs
	RingBuffer<float> audio;
	RingBuffer<char> video;

  MessageBus() {}

	MessageBus(size_t inputBufferSize, size_t audioBufferSize, size_t videoBufferSize)
		: buttons(inputBufferSize), link(inputBufferSize), audio(audioBufferSize), video(videoBufferSize) {}
};
