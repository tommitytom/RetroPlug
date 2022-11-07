#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IGraphics_include_in_plug_hdr.h"

#include "audio/AudioBuffer.h"
#include "application/Application.h"

using namespace iplug;
using namespace igraphics;

class FrameworkInstrument final : public Plugin {
private:
	std::shared_ptr<fw::app::Application> _app;
	fw::StereoAudioBuffer _input;
	fw::StereoAudioBuffer _output;

public:
	FrameworkInstrument(const InstanceInfo& info);
	~FrameworkInstrument() = default;

#if IPLUG_DSP // http://bit.ly/2S64BDd
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames);
	void ProcessMidiMsg(const IMidiMsg& msg) override {}
	void OnReset() override {}
	void OnParamChange(int paramIdx) override {}
	void OnIdle() override {}
	bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override { return false; }
#endif
};
