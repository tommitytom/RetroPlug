#include "RetroPlugInstrument.h"

#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "plugin/RetroPlugView.h"

RetroPlugInstrument::RetroPlugInstrument(const InstanceInfo& info)
	: Plugin(info, MakeConfig(0, 0))/*, _controller(GetSampleRate()) */
{
	// FIXME: Choose a more realistic size for this based on GetBlockSize()
	_sampleScratch = new float[1024 * 1024];

	const char* names[2] = { "C:\\retro\\LSDj-v5.0.3.gb", "C:\\retro\\LSDj-v5.0.3.sav" };
	_retroPlug.getUiContext().onDrop(2, names);

	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		//pGraphics->AttachCornerResizer(kUIResizerScale, false);
		pGraphics->AttachPanelBackground(COLOR_BLACK);
		pGraphics->LoadFont("Roboto-Regular", GAMEBOY_FN);
		pGraphics->LoadFont("Early-Gameboy", GAMEBOY_FN);

		pGraphics->SetKeyHandlerFunc([&](const IKeyPress& key, bool isUp) {
			return _retroPlug.getUiContext().onKey((VirtualKey::Enum)key.VK, !isUp);
		});

		pGraphics->AttachControl(new RetroPlugView(IRECT { 0, 0, 320, 288 }, &_retroPlug));

		OnReset();
	};
}

RetroPlugInstrument::~RetroPlugInstrument() {
	delete[] _sampleScratch;
}

void RetroPlugInstrument::ProcessBlock(sample** inputs, sample** outputs, int frameCount) {
	for (size_t j = 0; j < MaxNChannels(ERoute::kOutput); j++) {
		for (size_t i = 0; i < frameCount; i++) {
			outputs[j][i] = 0;
		}
	}

	f32* target = new f32[(size_t)frameCount * 2];
	_retroPlug.getAudioContext().setSampleRate(GetSampleRate());
	_retroPlug.getAudioContext().process(target, frameCount);

	for (size_t chanIdx = 0; chanIdx < 2; ++chanIdx) {
		for (size_t i = 0; i < frameCount; ++i) {
			outputs[chanIdx][i] = target[i * 2];
		}
	}

	delete[] target;

	//TimeInfo& timeInfo = _controller.getTimeInfo();
	//static_assert(sizeof(TimeInfo) == sizeof(iplug::ITimeInfo), "Time info size is incorrect");
	//memcpy(&timeInfo, &mTimeInfo, sizeof(TimeInfo));

	//_controller.audioController()->process(outputs, (size_t)frameCount);
}

void RetroPlugInstrument::OnIdle() {
	
}

bool RetroPlugInstrument::OnKeyDown(const IKeyPress& key) {
	// Ascii keys do not receive a correct key up event in ableton.  This is to
	// workaround that.
	
	if (GetHost() == EHost::kHostAbletonLive && key.utf8[0] != 0) {
		_heldKeys.push_back(key.VK);
	}

	return GetUI()->OnKeyDown(0, 0, key); 
}

bool RetroPlugInstrument::OnKeyUp(const IKeyPress& key) {
	IKeyPress keyCopy = key;
	
	if (GetHost() == EHost::kHostAbletonLive && key.VK == 0) {
		if (_heldKeys.size()) {
			keyCopy.VK = _heldKeys.back();
			_heldKeys.pop_back();
		}
	}

	return GetUI()->OnKeyUp(0, 0, keyCopy); 
}

/*bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) {
	DataBufferPtr buffer = _controller.saveState();
	int size = (int)buffer->size();
	chunk.Put(&size);
	chunk.PutBytes(buffer->data(), buffer->size());
	return true;
}*/

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	/*int size;
	pos = chunk.Get(&size, pos);

	if (size > 0 && size < 10 * 1024 * 1024) {
		DataBufferPtr buffer = std::make_shared<DataBuffer<char>>(size);
		chunk.GetBytes(buffer->data(), size, pos);
		_controller.loadState(buffer);
		return pos + size;
	}

	return pos;*/
	return pos;
}

void RetroPlugInstrument::ProcessMidiMsg(const IMidiMsg& msg) {
	TRACE;

	/*_controller.getMenuLock()->lock(); // Temporary
	if (_controller.audioLua()) {
		_controller.audioLua()->onMidi(msg.mOffset, msg.mStatus, msg.mData1, msg.mData2);
	}
	
	_controller.getMenuLock()->unlock();*/
}

void RetroPlugInstrument::OnReset() {
	//AudioSettings settings = { (size_t)NOutChansConnected(), 0, GetSampleRate() };
	//_controller.audioController()->setAudioSettings(settings);
}
