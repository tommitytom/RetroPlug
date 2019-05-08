#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "src/ui/EmulatorView.h"

RetroPlugInstrument::RetroPlugInstrument(IPlugInstanceInfo instanceInfo)
: IPLUG_CTOR(0, 0, instanceInfo)
{
	_sampleScratch = new float[1024 * 1024];

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		//pGraphics->AttachCornerResizer(kUIResizerScale, false);
		pGraphics->AttachPanelBackground(COLOR_BLACK);
		pGraphics->HandleMouseOver(true);
		pGraphics->LoadFont("Roboto-Regular", ROBOTTO_FN);

		const IRECT b = pGraphics->GetBounds();
		float mid = b.H() / 2;
		IRECT topRow(b.L, mid - 25, b.R, mid);
		IRECT bottomRow(b.L, mid, b.R, mid + 25);

		pGraphics->AttachControl(new ITextControl(topRow, "Double click to", IText(23, COLOR_WHITE)));
		pGraphics->AttachControl(new ITextControl(bottomRow, "load a ROM...", IText(23, COLOR_WHITE)));
		pGraphics->AttachControl(new EmulatorView(b, &_plug));
	};
#endif
}

RetroPlugInstrument::~RetroPlugInstrument() {
	delete[] _sampleScratch;
}

#if IPLUG_DSP
void RetroPlugInstrument::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	if (nFrames == 0) {
		return;
	}

	SameBoyPlugPtr plugPtr = _plug.plug();
	if (!plugPtr) {
		return;
	}

	SameBoyPlug* plug = plugPtr.get();
	MessageBus* bus = plug->messageBus();

	generateMidiClock(plug, nFrames);

	size_t frameCount = nFrames;
	size_t sampleCount = frameCount * 2;
	//size_t available = bus->audio.readAvailable();

	plug->update(frameCount);
	size_t available = bus->audio.readAvailable();
	assert(available == sampleCount);

	memset(_sampleScratch, 0, sampleCount * sizeof(float));
	size_t readAmount = bus->audio.read(_sampleScratch, sampleCount);
	assert(readAmount == sampleCount);

	float gain = 1.f;

	for (size_t i = 0; i < frameCount; i++) {
		outputs[0][i] = _sampleScratch[i * 2] * gain;
		outputs[1][i] = _sampleScratch[i * 2 + 1] * gain;
	}
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk & chunk) const {
	chunk.Resize(10);
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk & chunk, int startPos) {
	return 0;
}

void RetroPlugInstrument::generateMidiClock(SameBoyPlug* plug, int frameCount) {
	Lsdj& lsdj = _plug.lsdj();
	if (mTimeInfo.mTransportIsRunning) {
		switch (lsdj.syncMode) {
			case LsdjSyncModes::Slave:
				processSync(plug, frameCount, 1, 0xF8);
				break;
			case LsdjSyncModes::SlaveArduinoboy:
				if (lsdj.arduinoboyPlaying) {
					processSync(plug, frameCount, lsdj.tempoDivisor, 0xF8);
				}
				break;
			case LsdjSyncModes::MidiMap:
				if (lsdj.arduinoboyPlaying) {
					processSync(plug, frameCount, 1, 0xFF);
				}
				break;
		}
	}
}

void RetroPlugInstrument::processSync(SameBoyPlug* plug, int sampleCount, int tempoDivisor, char value) {
	double samplesPerMs = GetSampleRate() / 1000;
	double beatLen = (60000 / mTimeInfo.mTempo) * samplesPerMs;
	double qnLen = sampleCount / beatLen;
	double ppq24Len = qnLen * (24 / tempoDivisor);

	double ppq24 = mTimeInfo.mPPQPos * (24 / tempoDivisor);
	double nextPpq24 = ppq24 + ppq24Len;

	bool sync = ppq24 == 0 || (int)ppq24 != (int)nextPpq24;

	if (sync) {
		plug->sendMidiByte(0, value);
	}

	_lastPpq24 = ppq24;
}

void RetroPlugInstrument::OnReset() {
	_plug.setSampleRate(GetSampleRate());
}

void RetroPlugInstrument::ProcessMidiMsg(const IMidiMsg& msg) {
	TRACE;

	SameBoyPlugPtr plugPtr = _plug.plug();
	if (!plugPtr) {
		return;
	}

	Lsdj& lsdj = _plug.lsdj();
	if (lsdj.found) {
		switch (lsdj.syncMode) {
			case LsdjSyncModes::SlaveArduinoboy:
				if (msg.StatusMsg() == IMidiMsg::kNoteOn) {
					switch (msg.NoteNumber()) {
						case 24: lsdj.arduinoboyPlaying = true; break;
						case 25: lsdj.arduinoboyPlaying = false; break;
						case 26: lsdj.tempoDivisor = 1; break;
						case 27: lsdj.tempoDivisor = 2; break;
						case 28: lsdj.tempoDivisor = 4; break;
						case 29: lsdj.tempoDivisor = 8; break;
						default:
							if (msg.NoteNumber() > 29) {
								// TODO: Send row number
							}
					}
				}

				break;
			case LsdjSyncModes::MidiMap:
				if (msg.StatusMsg() == IMidiMsg::kNoteOn) {
					// Select row
					plugPtr->sendMidiByte(0, msg.NoteNumber());
				}

				break;
		}
	} else {
		// Presume mGB
		int status = msg.StatusMsg();
		char midiData[3];
		midiData[0] = msg.mStatus;
		midiData[1] = msg.mData1;
		midiData[2] = msg.mData2;

		plugPtr->sendMidiBytes(0, (const char*)midiData, 3);
	}
}

void RetroPlugInstrument::OnParamChange(int paramIdx) {
}
#endif
