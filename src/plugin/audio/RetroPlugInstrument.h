#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "controller/RetroPlugController.h"

using namespace iplug;
using namespace igraphics;

class RetroPlugInstrument final : public Plugin {
private:
	float* _sampleScratch;
	bool _transportRunning = false;

	RetroPlugController _controller;

	// This vector contains ascii keys that are currently held.  It is to work around a bug
	// in Ableton Live, where it doesn't tell is what key is being released during a key up event.
	std::vector<int> _heldKeys;

public:
	RetroPlugInstrument(const InstanceInfo& info);
	~RetroPlugInstrument();

#if IPLUG_DSP
public:
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnIdle() override;
	bool OnKeyDown(const IKeyPress& key) override;
	bool OnKeyUp(const IKeyPress& key) override;

	bool SerializeState(IByteChunk& chunk) override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;
private:
	inline double FramesToMs(int frameCount) const { return frameCount / (GetSampleRate() / 1000); }
#endif
};
