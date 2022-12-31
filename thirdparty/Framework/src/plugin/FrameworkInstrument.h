#pragma once

#include <vector>

#include "IPlug_include_in_plug_hdr.h"
#include "IGraphics_include_in_plug_hdr.h"

#include "audio/AudioBuffer.h"
#include "application/Application.h"

using namespace iplug;
using namespace igraphics;

class FrameworkInstrument final : public Plugin {
private:
	fw::audio::AudioManagerPtr _audioManager;
	fw::app::UiContextPtr _uiContext;
	fw::app::WindowPtr _window;

	fw::StereoAudioBuffer _input;
	fw::StereoAudioBuffer _output;

	// This vector contains ascii keys that are currently held.  It is to work around a bug
	// in Ableton Live, where it doesn't tell is what key is being released during a key up event.
	std::vector<int> _heldKeys;

public:
	FrameworkInstrument(const InstanceInfo& info);
	~FrameworkInstrument() = default;

#if IPLUG_DSP // http://bit.ly/2S64BDd
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames);
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnParamChange(int paramIdx) override {}
	//void OnIdle() override;
	bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override { return false; }
	bool OnKeyDown(const IKeyPress& key) override;
	bool OnKeyUp(const IKeyPress& key) override;
	//bool SerializeState(IByteChunk& chunk) const override;
	//int UnserializeState(const IByteChunk& chunk, int startPos) override;
#endif
};
