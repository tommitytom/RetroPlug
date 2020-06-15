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

	std::set<std::thread::id> _audioThreadIds;
	std::set<std::thread::id> _uiThreadIds;

public:
	RetroPlugInstrument(const InstanceInfo& info);
	~RetroPlugInstrument();

#if IPLUG_DSP
public:
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnIdle() override;
	bool OnKeyDown(const IKeyPress& key) { _uiThreadIds.insert(std::this_thread::get_id()); return GetUI()->OnKeyDown(0, 0, key); }
	bool OnKeyUp(const IKeyPress& key) { _uiThreadIds.insert(std::this_thread::get_id()); return GetUI()->OnKeyUp(0, 0, key); }

	bool SerializeState(IByteChunk& chunk) override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;
private:
	void GenerateMidiClock(SameBoyPlug* plug, int frameCount, bool transportChanged);
	void HandleTransportChange(SameBoyPlug* plug, bool running);
	void ProcessSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value);
	void ProcessInstanceMidiMessage(SameBoyPlug* plug, const IMidiMsg& msg, int channel);

	void ChangeLsdjKeyboardOctave(SameBoyPlug* plug, int octave, int offset);
	void ChangeLsdjInstrument(SameBoyPlug* plug, int instrument, int offset);

	inline double FramesToMs(int frameCount) const { return frameCount / (GetSampleRate() / 1000); }
#endif
};
