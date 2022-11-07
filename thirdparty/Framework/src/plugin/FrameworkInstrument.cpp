#include "FrameworkInstrument.h"

#include "IPlug_include_in_plug_src.h"
#include "IGraphics_include_in_plug_src.h"

#include "FrameworkView.h"
#include "application/Application.h"

FrameworkInstrument::FrameworkInstrument(const InstanceInfo& info) : Plugin(info, MakeConfig(0, 0)) {
#if IPLUG_EDITOR 
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		IGraphicsFramework* gfx = static_cast<IGraphicsFramework*>(pGraphics);

		pGraphics->EnableMouseOver(true);
		pGraphics->EnableMultiTouch(true);

		pGraphics->AttachPanelBackground(COLOR_GRAY);

		auto view = new FrameworkView(IRECT(0, 0, PLUG_WIDTH, PLUG_HEIGHT), gfx->GetNativeWindowHandle());
		_app = view->getApplication();

		pGraphics->AttachControl(view);
	};
#endif
}

void FrameworkInstrument::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	if (!_app || !_app->getAudioManager() || !_app->getAudioManager()->getProcessor()) {
		return;
	}

	_output.resize((uint32)nFrames);

	if (inputs) {
		_input.resize((uint32)nFrames);

		for (uint32 i = 0; i < _input.getFrameCount(); ++i) {
			for (uint32 j = 0; j < _input.ChannelCount; ++j) {
				_input.setSample(i, j, inputs[j][i]);
			}
		}
	}

	_app->getAudioManager()->process(_output.getSamples(), _input.getSamples(), (uint32)nFrames);

	for (uint32 i = 0; i < _output.getFrameCount(); ++i) {
		for (uint32 j = 0; j < _output.ChannelCount; ++j) {
			outputs[j][i] = _output.getSample(i, j);
		}
	}
}
