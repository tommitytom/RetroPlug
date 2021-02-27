#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "view/SystemView.h"
#include "view/RetroPlugView.h"

#include "util/File.h"
#include "util/zipp.h"

//#include "micromsg2/api2.h"

RetroPlugInstrument::RetroPlugInstrument(const InstanceInfo& info)
	: Plugin(info, MakeConfig(0, 0)), _controller(GetSampleRate()) 
{
	// FIXME: Choose a more realistic size for this based on GetBlockSize()
	_sampleScratch = new float[1024 * 1024];

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		//pGraphics->AttachCornerResizer(kUIResizerScale, false);
		pGraphics->AttachPanelBackground(COLOR_BLACK);
		pGraphics->LoadFont("Roboto-Regular", GAMEBOY_FN);
		pGraphics->LoadFont("Early-Gameboy", GAMEBOY_FN);

		pGraphics->SetKeyHandlerFunc([&](const IKeyPress& key, bool isUp) {
			return _controller.onKey((VirtualKey)key.VK, !isUp);
		});

		_controller.init(pGraphics->GetBounds());
		pGraphics->AttachControl(_controller.getView());

		OnReset();
	};
#endif
}

RetroPlugInstrument::~RetroPlugInstrument() {
	delete[] _sampleScratch;
}

#if IPLUG_DSP
void RetroPlugInstrument::ProcessBlock(sample** inputs, sample** outputs, int frameCount) {
	for (size_t j = 0; j < MaxNChannels(ERoute::kOutput); j++) {
		for (size_t i = 0; i < frameCount; i++) {
			outputs[j][i] = 0;
		}
	}

	TimeInfo& timeInfo = _controller.getTimeInfo();
	static_assert(sizeof(TimeInfo) == sizeof(iplug::ITimeInfo), "Time info size is incorrect");
	memcpy(&timeInfo, &mTimeInfo, sizeof(TimeInfo));

	_controller.audioController()->process(outputs, (size_t)frameCount);
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) {
	DataBufferPtr buffer = _controller.saveState();
	int size = (int)buffer->size();
	chunk.Put(&size);
	chunk.PutBytes(buffer->data(), buffer->size());
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	int size;
	pos = chunk.Get(&size, pos);

	if (size > 0 && size < 10 * 1024 * 1024) {
		DataBufferPtr buffer = std::make_shared<DataBuffer<char>>(size);
		chunk.GetBytes(buffer->data(), size, pos);
		_controller.loadState(buffer);
		return pos + size;
	}

	return pos;
}

void RetroPlugInstrument::ProcessMidiMsg(const IMidiMsg& msg) {
	TRACE;
	_controller.getMenuLock()->lock(); // Temporary
	if (_controller.audioLua()) {
		_controller.audioLua()->onMidi(msg.mOffset, msg.mStatus, msg.mData1, msg.mData2);
	}
	
	_controller.getMenuLock()->unlock();
}

void RetroPlugInstrument::OnReset() {
	AudioSettings settings = { (size_t)NOutChansConnected(), 0, GetSampleRate() };
	_controller.audioController()->setAudioSettings(settings);
}
#endif
