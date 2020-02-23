#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "view/EmulatorView.h"
#include "view/RetroPlugRoot.h"

RetroPlugInstrument::RetroPlugInstrument(const InstanceInfo& info)
	: Plugin(info, MakeConfig(0, 0)) {
	// FIXME: Choose a more realistic size for this based on GetBlockSize()
	_sampleScratch = new float[1024 * 1024];

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		_controller.init(pGraphics, GetHost(), &mTimeInfo, &_menuLock);
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

	// TODO: This mutex is temporary until I find a good way of sending context menus
	// across threads!
	std::scoped_lock lock(_menuLock);

	AudioLuaContext* lua = _controller.audioLua();
	lua->update();

	ProcessingContext* context = _controller.processingContext();
	context->process(outputs, (size_t)frameCount);
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) const {
	/*std::string target;
	serialize(target, _plug);
	chunk.PutStr(target.c_str());*/
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	/*WDL_String data;
	pos = chunk.GetStr(data, pos);
	deserialize(data.Get(), _plug);*/
	return pos;
}

void RetroPlugInstrument::GenerateMidiClock(SameBoyPlug* plug, int frameCount, bool transportChanged) {
	/*Lsdj& lsdj = plug->lsdj();
	if (transportChanged && plug->midiSync() && !lsdj.found) {
		if (mTimeInfo.mTransportIsRunning) {
			plug->sendSerialByte(0, 0xFA);
		} else {
			plug->sendSerialByte(0, 0xFC);
		}
	}

	if (mTimeInfo.mTransportIsRunning) {
		if (lsdj.found) {
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
		} else if (plug->midiSync()) {
			ProcessSync(plug, frameCount, 1, 0xF8);
		}
	}*/
}

void RetroPlugInstrument::HandleTransportChange(SameBoyPlug* plug, bool running) {
	/*if (plug->lsdj().autoPlay) {
		_buttonQueue.press(ButtonTypes::Start);
		consoleLogLine("Pressing start");
	}

	if (!_transportRunning && plug->lsdj().found && plug->lsdj().lastRow != -1) {
		plug->sendSerialByte(0, 0xFE);
	}*/
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
	std::scoped_lock lock(_menuLock); // Temporary
	_controller.audioLua()->onMidi(msg.mOffset, msg.mStatus, msg.mData1, msg.mData2);
}

unsigned char reverse(unsigned char b) {
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	return b;
}

void RetroPlugInstrument::ProcessInstanceMidiMessage(SameBoyPlug* plug, const IMidiMsg& msg, int channel) {
	/*Lsdj& lsdj = plug->lsdj();
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
						plug->sendSerialByte(msg.mOffset, msg.NoteNumber() - 30);
					}
				}
			}

			break;
		case LsdjSyncModes::MidiMap:
			switch (msg.StatusMsg()) {
			case IMidiMsg::kNoteOn: {
				int rowIdx = midiMapRowNumber(channel, msg.NoteNumber());
				if (rowIdx != -1) {
					plug->sendSerialByte(msg.mOffset, rowIdx);
					lsdj.lastRow = rowIdx;
				}

				break;
			}
			case IMidiMsg::kNoteOff:
				int rowIdx = midiMapRowNumber(channel, msg.NoteNumber());
				if (rowIdx == lsdj.lastRow) {
					plug->sendSerialByte(msg.mOffset, 0xFE);
					lsdj.lastRow = -1;
				}

				break;
			}

			break;
		case LsdjSyncModes::KeyboardArduinoboy:
			if (plug->lsdj().currentInstrument == -1) {
				for (int i = 0; i < 8; i++) {
					plug->sendSerialByte(msg.mOffset, (reverse((unsigned char)LsdjKeyboard::InsDn) >> 1));
				}

				plug->lsdj().currentInstrument = 0;
			}

			switch (msg.StatusMsg()) {
			case IMidiMsg::kNoteOn: {
				int note = msg.NoteNumber();

				if (note >= LsdjKeyboardNoteStart) {
					note -= LsdjKeyboardNoteStart;

					//ChangeLsdjKeyboardOctave(plug, msg.NoteNumber() / 12, 0);

					if (note >= 0x3C) {
						// Use second row of keyboard keys
						note = (note % 12) + 0x0C;
					} else {
						note = (note % 12);
					}

					plug->sendSerialByte(msg.mOffset, reverse(LsdjKeyboardNoteMap[note]) >> 1);
				} else if (note >= LsdjKeyboardStartOctave) {
					note -= LsdjKeyboardStartOctave;
					uint8_t command = LsdjKeyboardLowOctaveMap[note];
					if (command == 0x68 || command == 0x72 || command == 0x74 || command == 0x75) {
						//cursor values need an "extended" pc keyboard mode message
						plug->sendSerialByte(msg.mOffset, reverse(0xE0) >> 1);
					}

					plug->sendSerialByte(msg.mOffset, reverse(command) >> 1);
				}
				
				break;
			}
			case IMidiMsg::kProgramChange: {
				ChangeLsdjInstrument(plug, msg.Program(), msg.mOffset);
				break;
			}
			case IMidiMsg::kNoteOff:
				break;
			}
			break;
		}
	} else {
		// Presume mGB or any other MIDI enabled rom
		char midiData[3];
		midiData[0] = channel | (msg.StatusMsg() << 4);
		midiData[1] = msg.mData1;
		midiData[2] = msg.mData2;

		plug->sendMidiBytes(msg.mOffset, (const char*)midiData, 3);
	}*/
}

void RetroPlugInstrument::ChangeLsdjKeyboardOctave(SameBoyPlug* plug, int octave, int offset) {
	/*int current = plug->lsdj().currentOctave;
	if (octave != current) {
		LsdjKeyboard key = LsdjKeyboard::OctDn;
		if (octave > current) {
			key = LsdjKeyboard::OctUp;
		}

		key = (LsdjKeyboard)(reverse((unsigned char)key) >> 1);

		int diff = abs(octave - current);
		while (diff--) {
			plug->sendSerialByte(offset, (char)key);
		}

		plug->lsdj().currentOctave = octave;
	}*/
}

void RetroPlugInstrument::ChangeLsdjInstrument(SameBoyPlug * plug, int instrument, int offset) {
	/*int current = plug->lsdj().currentInstrument;
	if (current != instrument) {
		LsdjKeyboard key = LsdjKeyboard::InsDn;
		if (instrument > current) {
			key = LsdjKeyboard::InsUp;
		}

		key = (LsdjKeyboard)(reverse((unsigned char)key) >> 1);

		int diff = abs(current - instrument);
		while (diff--) {
			plug->sendSerialByte(offset, (char)key);
		}

		plug->lsdj().currentInstrument = instrument;
	}*/
}

void RetroPlugInstrument::OnReset() {
	AudioSettings settings = { (size_t)NOutChansConnected(), 0, GetSampleRate() };
	_controller.processingContext()->setAudioSettings(settings);
}
#endif
