#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ButtonQueue.h"
#include "controller/RetroPlugController.h"

using namespace iplug;
using namespace igraphics;

class RetroPlugInstrument : public Plugin {
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

	bool SerializeState(IByteChunk& chunk) const override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;
private:
	void GenerateMidiClock(SameBoyPlug* plug, int frameCount, bool transportChanged);
	void HandleTransportChange(SameBoyPlug* plug, bool running);
	void ProcessSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value);
	void ProcessInstanceMidiMessage(SameBoyPlug* plug, const IMidiMsg& msg, int channel);

	void ChangeLsdjKeyboardOctave(SameBoyPlug* plug, int octave, int offset);
	void ChangeLsdjInstrument(SameBoyPlug* plug, int instrument, int offset);

	inline double FramesToMs(int frameCount) const { return frameCount / (GetSampleRate() / 1000); }

	float* _sampleScratch;
	bool _transportRunning = false;

	ButtonQueue _buttonQueue;

	RetroPlugController _controller;
#endif
};
