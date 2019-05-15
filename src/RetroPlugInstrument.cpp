#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "src/ui/EmulatorView.h"
#include "util/Serializer.h"

RetroPlugInstrument::RetroPlugInstrument(IPlugInstanceInfo instanceInfo)
: IPLUG_CTOR(0, 0, instanceInfo) {
	// FIXME: Choose a more realistic size for this based on GetBlockSize()
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

		EmulatorView* emulatorView = new EmulatorView(b, &_plug);
		pGraphics->AttachControl(emulatorView);

		pGraphics->SetKeyHandlerFunc([emulatorView](const IKeyPress& key, bool isUp) {
			return emulatorView->OnKey(key, !isUp);
		});
	};
#endif
}

RetroPlugInstrument::~RetroPlugInstrument() {
	delete[] _sampleScratch;
}

#if IPLUG_DSP
void RetroPlugInstrument::ProcessBlock(sample** inputs, sample** outputs, int frameCount) {
	if (frameCount == 0) {
		return;
	}

	SameBoyPlugPtr plugPtr = _plug.plug();
	if (!plugPtr) {
		return;
	}

	SameBoyPlug* plug = plugPtr.get();

	// I know it's bad to use a mutex here, but its only used when making settings changes
	// or saving SRAM data to disk.  Should still probably swap this out for a different
	// method at some point, but it works for now.
	plug->lock().lock();

	MessageBus* bus = plug->messageBus();

	if (_transportRunning != mTimeInfo.mTransportIsRunning) {
		_transportRunning = mTimeInfo.mTransportIsRunning;
		consoleLogLine("Transport running: " + std::to_string(_transportRunning));
		HandleTransportChange(plug, _transportRunning);
	}

	_buttonQueue.update(bus, FramesToMs(frameCount));
	GenerateMidiClock(plug, frameCount);

	int sampleCount = frameCount * 2;

	plug->update(frameCount);
	size_t available = bus->audio.readAvailable();
	assert(available == sampleCount);

	memset(_sampleScratch, 0, sampleCount * sizeof(float));
	size_t readAmount = bus->audio.read(_sampleScratch, sampleCount);
	assert(readAmount == sampleCount);

	for (size_t i = 0; i < frameCount; i++) {
		outputs[0][i] = _sampleScratch[i * 2];
		outputs[1][i] = _sampleScratch[i * 2 + 1];
	}

	plug->lock().unlock();
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) const {
	if (_plug.romPath().size() == 0) {
		return true;
	}
	
	Serialize(chunk, _plug);
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int startPos) {
	return Deserialize(chunk, _plug, startPos);
}

void RetroPlugInstrument::GenerateMidiClock(SameBoyPlug* plug, int frameCount) {
	Lsdj& lsdj = _plug.lsdj();
	if (mTimeInfo.mTransportIsRunning) {
		switch (lsdj.syncMode) {
			case LsdjSyncModes::Midi:
				ProcessSync(plug, frameCount, 1, 0xF8);
				break;
			case LsdjSyncModes::MidiArduinoboy:
				if (lsdj.arduinoboyPlaying) {
					ProcessSync(plug, frameCount, lsdj.tempoDivisor, 0xF8);
				}
				break;
			case LsdjSyncModes::MidiMap:
				ProcessSync(plug, frameCount, 1, 0xFF);
				break;
		}
	}
}

void RetroPlugInstrument::HandleTransportChange(SameBoyPlug* plug, bool running) {
	if (_plug.lsdj().autoPlay) {
		_buttonQueue.press(ButtonType::GB_KEY_START);
		consoleLogLine("Pressing start");
	}

	if (!_transportRunning && _plug.lsdj().found && _plug.lsdj().lastRow != -1) {
		plug->sendMidiByte(0, 0xFE);
	}
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
		plug->sendMidiByte(offset, value);
	}
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
			case LsdjSyncModes::MidiArduinoboy:
				if (msg.StatusMsg() == IMidiMsg::kNoteOn) {
					switch (msg.NoteNumber()) {
						case 24: lsdj.arduinoboyPlaying = true; break;
						case 25: lsdj.arduinoboyPlaying = false; break;
						case 26: lsdj.tempoDivisor = 1; break;
						case 27: lsdj.tempoDivisor = 2; break;
						case 28: lsdj.tempoDivisor = 4; break;
						case 29: lsdj.tempoDivisor = 8; break;
						default:
							if (msg.NoteNumber() >= 30) {
								plugPtr->sendMidiByte(msg.mOffset, msg.NoteNumber() - 30);
							}
					}
				}

				break;
			case LsdjSyncModes::MidiMap:
				switch (msg.StatusMsg()) {
				case IMidiMsg::kNoteOn: {
					int rowIdx = midiMapRowNumber(msg.Channel(), msg.NoteNumber());
					if (rowIdx != -1) {
						plugPtr->sendMidiByte(msg.mOffset, rowIdx);
						lsdj.lastRow = rowIdx;
					}

					break;
				}
				case IMidiMsg::kNoteOff:
					int rowIdx = midiMapRowNumber(msg.Channel(), msg.NoteNumber());
					if (rowIdx == lsdj.lastRow) {
						plugPtr->sendMidiByte(msg.mOffset, 0xFE);
						lsdj.lastRow = -1;
					}

					break;
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

		plugPtr->sendMidiBytes(msg.mOffset, (const char*)midiData, 3);
	}
}
#endif
