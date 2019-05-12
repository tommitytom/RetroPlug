#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "plugs/RetroPlug.h"

class RetroPlugInstrument : public IPlug {
public:
	RetroPlugInstrument(IPlugInstanceInfo instanceInfo);
	~RetroPlugInstrument();

#if IPLUG_DSP
public:
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnParamChange(int paramIdx) override;
	void OnIdle() override;

	bool SerializeState(IByteChunk& chunk) const override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;
private:
	void GenerateMidiClock(SameBoyPlug* plug, int frameCount);

	void ProcessSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value);

	RetroPlug _plug;
	float* _sampleScratch;
	bool _transportRunning = false;
#endif
};
