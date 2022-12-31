#include "FrameworkInstrument.h"

#include "IPlug_include_in_plug_src.h"
#include "IGraphics_include_in_plug_src.h"

#include "FrameworkView.h"
#include "application/Application.h"

#include "foundation/FoundationModule.h"
#include "foundation/MacroTools.h"

using namespace fw;

#include INCLUDE_EXAMPLE(EXAMPLE_IMPL)

FrameworkInstrument::FrameworkInstrument(const InstanceInfo& info) :
	Plugin(info, MakeConfig(0, 0)),
	_audioManager(std::make_shared<fw::audio::AudioManager>())
{
	FoundationModule::setup();

#if IPLUG_EDITOR 
	_uiContext = std::make_shared<app::UiContext>(_audioManager);

	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
	};

	this->SetSizeConstraints(10, 999999, 10, 999999);

	mLayoutFunc = [&](IGraphics* pGraphics) {
		IGraphicsFramework* gfx = static_cast<IGraphicsFramework*>(pGraphics);

		pGraphics->EnableMouseOver(true);
		pGraphics->EnableMultiTouch(true);

		pGraphics->AttachPanelBackground(COLOR_GRAY);

		if (!_window) {
			NativeWindowHandle nativeWindowHandle = gfx->GetNativeWindowHandle();
			_window = _uiContext->addNativeWindow<EXAMPLE_IMPL>(nativeWindowHandle, fw::Dimension{ PLUG_WIDTH, PLUG_HEIGHT });
		}

		auto view = new FrameworkView(*_uiContext, _window);

		pGraphics->AttachControl(view);
	};
#endif
}

void FrameworkInstrument::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	if (!_audioManager->getProcessor()) {
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

	_audioManager->process(_output.getSamples(), _input.getSamples(), (uint32)nFrames);

	for (uint32 i = 0; i < _output.getFrameCount(); ++i) {
		for (uint32 j = 0; j < _output.ChannelCount; ++j) {
			outputs[j][i] = _output.getSample(i, j);
		}
	}
}

void FrameworkInstrument::ProcessMidiMsg(const IMidiMsg& msg) {
	if (!_audioManager->getProcessor()) {
		return;
	}

	_audioManager->getProcessor()->onMidi(fw::MidiMessage{
		.status = msg.mStatus,
		.data1 = msg.mData1,
		.data2 = msg.mData2
	});
}

void FrameworkInstrument::OnReset() {
	//AudioSettings settings = { (size_t)NOutChansConnected(), 0, GetSampleRate() };
	//_controller.audioController()->setAudioSettings(settings);
}
/*
void FrameworkInstrument::OnIdle() {
	
}
*/
bool FrameworkInstrument::OnKeyDown(const IKeyPress& key) {
	// Ascii keys do not receive a correct key up event in ableton.  This is to
	// workaround that.

	if (GetHost() == EHost::kHostAbletonLive && key.utf8[0] != 0) {
		_heldKeys.push_back(key.VK);
	}

	return GetUI()->OnKeyDown(0, 0, key);
}

bool FrameworkInstrument::OnKeyUp(const IKeyPress& key) {
	IKeyPress keyCopy = key;

	if (GetHost() == EHost::kHostAbletonLive && key.VK == 0) {
		if (_heldKeys.size()) {
			keyCopy.VK = _heldKeys.back();
			_heldKeys.pop_back();
		}
	}

	return GetUI()->OnKeyUp(0, 0, keyCopy);
}

/*bool FrameworkInstrument::SerializeState(IByteChunk& chunk) const {
	/*DataBufferPtr buffer = _controller.saveState();
	int size = (int)buffer->size();
	chunk.Put(&size);
	chunk.PutBytes(buffer->data(), buffer->size());
	return true;*//*
	return false;
}

int FrameworkInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	/*int size;
	pos = chunk.Get(&size, pos);

	if (size > 0 && size < 10 * 1024 * 1024) {
		DataBufferPtr buffer = std::make_shared<DataBuffer<char>>(size);
		chunk.GetBytes(buffer->data(), size, pos);
		_controller.loadState(buffer);
		return pos + size;
	}

	return pos;*//*
	return 0;
}*/
