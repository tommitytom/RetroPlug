#pragma once

#include "IPlug_include_in_plug_hdr.h"
//#include "IGraphics_include_in_plug_hdr.h"
#include "IControls.h"

using namespace iplug;
using namespace igraphics;

class FrameworkInstrument final : public Plugin {
public:
	FrameworkInstrument(const InstanceInfo& info);
	~FrameworkInstrument() = default;

#if IPLUG_DSP // http://bit.ly/2S64BDd
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override {}
  void ProcessMidiMsg(const IMidiMsg& msg) override {}
  void OnReset() override {}
  void OnParamChange(int paramIdx) override {}
  void OnIdle() override {}
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override { return false; }
#endif
};
