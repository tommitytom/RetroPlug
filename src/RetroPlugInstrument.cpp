#include "RetroPlugInstrument.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "src/ui/EmulatorView.h"
#include "src/ui/RetroPlugRoot.h"
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

		RetroPlugRoot* root = new RetroPlugRoot(pGraphics->GetBounds(), &_plug);
		pGraphics->AttachControl(root);

		pGraphics->SetKeyHandlerFunc([root](const IKeyPress& key, bool isUp) {
			return root->OnKey(key, !isUp);
		});
	};
#endif
}

RetroPlugInstrument::~RetroPlugInstrument() {
	delete[] _sampleScratch;
}

#if IPLUG_DSP
void RetroPlugInstrument::ProcessBlock(sample** inputs, sample** outputs, int frameCount) {
	for (size_t j = 0; j < MaxNChannels(kOutput); j++) {
		for (size_t i = 0; i < frameCount; i++) {
			outputs[j][i] = 0;
		}
	}

	if (frameCount == 0 || !_plug.getPlug(0) || !_plug.getPlug(0)->active()) {
		return;
	}

	bool transportChanged = false;
	if (_transportRunning != mTimeInfo.mTransportIsRunning) {
		_transportRunning = mTimeInfo.mTransportIsRunning;
		transportChanged = true;
		consoleLogLine("Transport running: " + std::to_string(_transportRunning));
	}

	// Keeping the shared pointer here makes sure that the reference count
	// is at least 1 while we work with it
	SameBoyPlugPtr plugPtrs[MAX_INSTANCES];

	SameBoyPlug* plugs[MAX_INSTANCES] = { nullptr };
	SameBoyPlug* linkedPlugs[MAX_INSTANCES] = { nullptr };

	size_t totalPlugCount = 0;
	size_t plugCount = 0;
	size_t linkedPlugCount = 0;

	int sampleCount = frameCount * 2;

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		SameBoyPlugPtr plugPtr = _plug.plugs()[i];
		if (plugPtr && plugPtr->active()) {
			SameBoyPlug* plug = plugPtr.get();
			plugPtrs[i] = plugPtr;
			plugs[i] = plug;

			if (!plug->gameLink()) {
				plugs[plugCount++] = plug;
			} else {
				linkedPlugs[linkedPlugCount++] = plug;
			}

			totalPlugCount++;

			if (transportChanged) {
				HandleTransportChange(plug, _transportRunning);
			}

			// I know it's bad to use a mutex here, but its only used when making settings changes
			// or saving SRAM data to disk.  Should still probably swap this out for a different
			// method at some point, but it works for now.
			plug->lock().lock();

			MessageBus* bus = plug->messageBus();
			_buttonQueue.update(bus, FramesToMs(frameCount));
			GenerateMidiClock(plug, frameCount, transportChanged);
		}
	}

	for (size_t i = 0; i < plugCount; i++) {
		plugs[i]->update(frameCount);
	}

	if (linkedPlugCount > 0) {
		linkedPlugs[0]->updateMultiple(linkedPlugs, linkedPlugCount, frameCount);
	}

	int chanMultipler = 0;
	if (NOutChansConnected() == 8 && _plug.audioRouting() != AudioChannelRouting::StereoMixDown) {
		chanMultipler = 2;
	}

	for (size_t i = 0; i < totalPlugCount; i++) {
		SameBoyPlug* plug = plugPtrs[i].get();
		MessageBus* bus = plug->messageBus();

		size_t available = bus->audio.readAvailable();
		if (available == sampleCount) {
			memset(_sampleScratch, 0, sampleCount * sizeof(float));
			size_t readAmount = bus->audio.read(_sampleScratch, sampleCount);
			if (readAmount == sampleCount) {
				for (size_t j = 0; j < frameCount; j++) {
					outputs[i * chanMultipler][j] += _sampleScratch[j * 2];
					outputs[i * chanMultipler + 1][j] += _sampleScratch[j * 2 + 1];
				}
			}
		}

		plug->lock().unlock();
	}
}

void RetroPlugInstrument::OnIdle() {

}

bool RetroPlugInstrument::SerializeState(IByteChunk& chunk) const {
	std::string target;
	Serialize(target, _plug);
	chunk.PutStr(target.c_str());
	return true;
}

int RetroPlugInstrument::UnserializeState(const IByteChunk& chunk, int pos) {
	WDL_String data;
	pos = chunk.GetStr(data, pos);
	Deserialize(data.Get(), _plug);
	return pos;
}

void RetroPlugInstrument::GenerateMidiClock(SameBoyPlug* plug, int frameCount, bool transportChanged) {
	Lsdj& lsdj = plug->lsdj();
	if (transportChanged && plug->midiSync() && !lsdj.found) {
		if (mTimeInfo.mTransportIsRunning) {
			plug->sendMidiByte(0, 0xFA);
		} else {
			plug->sendMidiByte(0, 0xFC);
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
	}
}

void RetroPlugInstrument::HandleTransportChange(SameBoyPlug* plug, bool running) {
	if (plug->lsdj().autoPlay) {
		_buttonQueue.press(ButtonTypes::Start);
		consoleLogLine("Pressing start");
	}

	if (!_transportRunning && plug->lsdj().found && plug->lsdj().lastRow != -1) {
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

void RetroPlugInstrument::ProcessMidiMsg(const IMidiMsg& msg) {
	TRACE;

	auto plugs = _plug.plugs();
	size_t count = _plug.instanceCount();

	switch (_plug.midiRouting()) {
	case MidiChannelRouting::SendToAll: {
		for (size_t i = 0; i < count; i++) {
			SameBoyPlugPtr plug = plugs[i];
			if (plug && plug->active()) {
				ProcessInstanceMidiMessage(plug.get(), msg, msg.Channel());
			}
		}

		break;
	}
	case MidiChannelRouting::OneChannelPerInstance: {
		if (msg.Channel() < count) {
			SameBoyPlugPtr plug = plugs[msg.Channel()];
			if (plug && plug->active()) {
				ProcessInstanceMidiMessage(plug.get(), msg, 0);
			}
		}

		break;
	}
	case MidiChannelRouting::FourChannelsPerInstance: {
		if (msg.Channel() < count * 4) {
			SameBoyPlugPtr plug = plugs[msg.Channel() / 4];
			if (plug && plug->active()) {
				ProcessInstanceMidiMessage(plug.get(), msg, msg.Channel() % 4);
			}
		}

		break;
	}
	}
}

void RetroPlugInstrument::ProcessInstanceMidiMessage(SameBoyPlug* plug, const IMidiMsg& msg, int channel) {
	Lsdj& lsdj = plug->lsdj();
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
						plug->sendMidiByte(msg.mOffset, msg.NoteNumber() - 30);
					}
				}
			}

			break;
		case LsdjSyncModes::MidiMap:
			switch (msg.StatusMsg()) {
			case IMidiMsg::kNoteOn: {
				int rowIdx = midiMapRowNumber(channel, msg.NoteNumber());
				if (rowIdx != -1) {
					plug->sendMidiByte(msg.mOffset, rowIdx);
					lsdj.lastRow = rowIdx;
				}

				break;
			}
			case IMidiMsg::kNoteOff:
				int rowIdx = midiMapRowNumber(channel, msg.NoteNumber());
				if (rowIdx == lsdj.lastRow) {
					plug->sendMidiByte(msg.mOffset, 0xFE);
					lsdj.lastRow = -1;
				}

				break;
			}

			break;
		}
	} else {
		// Presume mGB
		char midiData[3];
		midiData[0] = channel | (msg.StatusMsg() << 4);
		midiData[1] = msg.mData1;
		midiData[2] = msg.mData2;

		plug->sendMidiBytes(msg.mOffset, (const char*)midiData, 3);
	}
}

void RetroPlugInstrument::OnReset() {
	_plug.setSampleRate(GetSampleRate());
}
#endif
