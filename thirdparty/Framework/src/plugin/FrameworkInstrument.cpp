#include "FrameworkInstrument.h"

#include "IPlug_include_in_plug_src.h"
#include "IGraphics_include_in_plug_src.h"

#include "FrameworkView.h"
#include "application/Application.h"
#include "entry/ApplicationFactory.h"

#include "graphics/gl/GlRenderContext.h"

using namespace fw;

FrameworkInstrument::FrameworkInstrument(const InstanceInfo& info) :
	Plugin(info, MakeConfig(0, 0)),
	_audioManager(std::make_shared<fw::audio::AudioManager>())
{
	_app = ApplicationFactory::create();
	_audioManager->setSampleRate((f32)GetSampleRate());
	_audioManager->setProcessor(_app->onCreateAudio());

#if IPLUG_EDITOR
	this->SetSizeConstraints(10, 999999, 10, 999999);

	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		assert(!_editorOpen);

		ViewPtr view = _app->onCreateUi();

		if (view) {
			IGraphicsFramework* gfx = static_cast<IGraphicsFramework*>(pGraphics);
			
			std::shared_ptr<app::UiContext> uiContext = std::make_shared<app::UiContext>(std::make_unique<GlRenderContext>(false));

			pGraphics->EnableMouseOver(true);
			pGraphics->EnableMultiTouch(true);
			pGraphics->AttachPanelBackground(COLOR_GRAY);

			NativeWindowHandle nativeWindowHandle = gfx->GetNativeWindowHandle();
			fw::app::WindowPtr window = uiContext->setupNativeWindow(view, nativeWindowHandle, fw::Dimension{ PLUG_WIDTH, PLUG_HEIGHT });

			ViewManagerPtr viewManager = window->getViewManager();
			viewManager->createState(_audioManager.get());
			viewManager->createState<EventNode>(_audioManager->getProcessor()->getEventNode().spawn("Ui"));

			FrameworkView* frameworkView = new FrameworkView(uiContext, window, [&]() { _editorOpen = false; });
			pGraphics->AttachControl(frameworkView);

			fw::Dimension dimensions = viewManager->getDimensions();
			pGraphics->Resize(dimensions.w, dimensions.h, 1.0f);
			//frameworkView->SetRECT(IRECT(0.0f, 0.0f, (f32)dimensions.w, (f32)dimensions.h));

			_editorOpen = true;
		}
	};
#endif
}

void FrameworkInstrument::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	if (!_audioManager->getProcessor()) {
		return;
	}

	checkTransportRunning();

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

	checkTransportRunning();

	_audioManager->getProcessor()->onMidi(fw::MidiMessage{
		.status = msg.mStatus,
		.data1 = msg.mData1,
		.data2 = msg.mData2
	});
}

void FrameworkInstrument::OnReset() {
	_audioManager->setSampleRate((f32)GetSampleRate());
	//AudioSettings settings = { (size_t)NOutChansConnected(), 0, GetSampleRate() };
	//_controller.audioController()->setAudioSettings(settings);
}

void FrameworkInstrument::OnIdle() {

}

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

bool FrameworkInstrument::SerializeState(IByteChunk& chunk) const {
	assert(_audioManager->getProcessor());
	
	fw::Uint8Buffer target;
	_audioManager->getProcessor()->onSerialize(target);

	uint32 size = target.size();
	chunk.Put(&size);

	if (size) {
		chunk.PutBytes(target.data(), target.size());
	}

	return true;
}

int FrameworkInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	assert(_audioManager->getProcessor());

	uint32 size;
	pos = chunk.Get(&size, pos);

	if (size <= chunk.Size() - pos) {
		fw::Uint8Buffer source((uint8*)chunk.GetData() + pos, size);
		_audioManager->getProcessor()->onDeserialize(source);
		return pos + size;
	}

	spdlog::error("Failed to deserialize state: Size is too large ({}).  Must be less than {}", size, chunk.Size() - pos);
	return pos;
}

bool FrameworkInstrument::checkTransportRunning() {
	if (mTimeInfo.mTransportIsRunning != _transportRunning) {
		_transportRunning = mTimeInfo.mTransportIsRunning;

		auto processor = _audioManager->getProcessor();
		if (processor) {
			processor->onTransportChange(_transportRunning);
		}
	}

	return _transportRunning;
}
