#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ButtonQueue.h"
#include "controller/RetroPlugController.h"

using namespace iplug;
using namespace igraphics;

class RetroPlugInstrument : public Plugin {
private:
	float* _sampleScratch;
	bool _transportRunning = false;

	ButtonQueue _buttonQueue;

	RetroPlugController _controller;

public:
	RetroPlugInstrument(const InstanceInfo& info);
	~RetroPlugInstrument();

#if IPLUG_DSP
public:
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnIdle() override;
	bool OnKeyDown(const IKeyPress& key) { return GetUI()->OnKeyDown(0, 0, key); }
	bool OnKeyUp(const IKeyPress& key) { return GetUI()->OnKeyUp(0, 0, key); }

	bool SerializeState(IByteChunk& chunk) override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;
private:
	void ProcessSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value);

	inline double FramesToMs(int frameCount) const { return frameCount / (GetSampleRate() / 1000); }
#endif
};
