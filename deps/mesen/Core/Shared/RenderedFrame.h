#pragma once
#include "pch.h"
#include "Shared/SettingTypes.h"
#include "Shared/ControlDeviceState.h"

struct RenderedFrame
{
	void* FrameBuffer = nullptr;
	void* Data = nullptr; //Used by HD packs
	uint32_t Width = 256;
	uint32_t Height = 240;
	double Scale = 1.0;
	uint32_t FrameNumber = 0;
	uint32_t VideoPhase = 0;
	vector<ControllerData> InputData;

	RenderedFrame()
	{}

	RenderedFrame(void* buffer, uint32_t width, uint32_t height, double scale = 1.0, uint32_t frameNumber = 0) :
		FrameBuffer(buffer),
		Data(nullptr),
		Width(width),
		Height(height),
		Scale(scale),
		FrameNumber(frameNumber),
		InputData({})
	{}

	// RetroPlug: the consoles this fork does not run (PCE, SNES, WS) still build a frame through this constructor, and
	// so does Mesen's own Game Boy (GbPpu), so they copy the port snapshot one extra time per frame. The NES, SMS and
	// GBA refill a member instead (Reset and SetInputData), so "no copies" is theirs and not universal.
	RenderedFrame(void* buffer, uint32_t width, uint32_t height, double scale, uint32_t frameNumber, vector<ControllerData> inputData, uint32_t videoPhase = 0) :
		FrameBuffer(buffer),
		Data(nullptr),
		Width(width),
		Height(height),
		Scale(scale),
		FrameNumber(frameNumber),
		VideoPhase(videoPhase),
		InputData(inputData)
	{}

	// RetroPlug: refill a frame in place, and set its input snapshot in place, so a core that emits one frame per
	// video frame keeps the frame object and the input vector's storage between frames. A frame built per video
	// frame allocates in the constructor's InputData copies, on the emulation thread, for no gain: nothing changes
	// between frames but the fields below.
	void Reset(void* buffer, uint32_t width, uint32_t height, double scale, uint32_t frameNumber, uint32_t videoPhase = 0)
	{
		FrameBuffer = buffer;
		Data = nullptr;
		Width = width;
		Height = height;
		Scale = scale;
		FrameNumber = frameNumber;
		VideoPhase = videoPhase;
	}

	void SetInputData(const vector<ControllerData>& inputData)
	{
		InputData.resize(inputData.size());
		for(size_t i = 0; i < inputData.size(); i++) {
			InputData[i].Type = inputData[i].Type;
			InputData[i].Port = inputData[i].Port;
			// RetroPlug: reserved before the copy, for the same reason the port snapshot is (see GetPortStates): a port
			// the control manager registers late arrives as a new element here, and its state must not be what grows
			// this frame's storage inside a block.
			if(InputData[i].State.State.capacity() < ControlDeviceState::MaxBytes) {
				InputData[i].State.State.reserve(ControlDeviceState::MaxBytes);
			}
			InputData[i].State.State = inputData[i].State.State;
		}
	}
};
