#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "view/SystemView.h"
#include "view/RetroPlugView.h"

#include "util/File.h"
#include "util/zipp.h"

RetroPlugInstrument::RetroPlugInstrument(const InstanceInfo& info)
	: Plugin(info, MakeConfig(0, 0)), _controller(&mTimeInfo, GetSampleRate()) 
{
	// FIXME: Choose a more realistic size for this based on GetBlockSize()
	_sampleScratch = new float[1024 * 1024];

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		_controller.init(pGraphics, GetHost());
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

	_controller.audioController()->process(outputs, (size_t)frameCount);
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) {
	DataBufferPtr buffer = _controller.saveState();
	chunk.PutBytes(buffer->data(), buffer->size());
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	int size = chunk.Size() - pos;
	DataBufferPtr buffer = std::make_shared<DataBuffer<char>>(size);
	chunk.GetBytes(buffer->data(), size, pos);

	_controller.loadState(buffer);

	return pos + size;
}

void RetroPlugInstrument::ProcessSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value) {
	int resolution = 24 / tempoDivisor;
	double samplesPerMs = GetSampleRate() / 1000.0;
	double beatLenMs = (60000.0 / mTimeInfo.mTempo);
	double beatLenSamples = beatLenMs * samplesPerMs;
	double beatLenSamples24 = beatLenSamples / resolution;

	double ppq24 = mTimeInfo.mPPQPos * resolution;
	double framePpqLen = (sampleCount / beatLenSamples) * resolution;

	double nextPpq24 = ppq24 + framePpqLen;

	bool sync = false;
	int offset = 0;
	if (ppq24 == 0) {
		sync = true;
	} else if ((int)ppq24 != (int)nextPpq24) {
		sync = true;
		double amount = ceil(ppq24) - ppq24;
		offset = (int)(beatLenSamples24 * amount);
		if (offset >= sampleCount) {
			//consoleLogLine(("Overshot: " + std::to_string(offset - sampleCount)));
			offset = sampleCount - 1;
		}
	}

	if (sync) {
		plug->sendSerialByte(offset, value);
	}
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
