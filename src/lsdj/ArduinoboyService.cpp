#include "ArduinoboyService.h"

namespace rp {
	int32 midiMapRowNumber(int32 channel, int32 note) {
		if (channel == 0) {
			return note;
		}

		if (channel == 1) {
			return note + 128;
		}

		return -1;
	}

	void ArduinoboyService::onMidi(System& system, const fw::MidiMessage& message) {
		switch (getRawState().syncMode) {
		case LsdjSyncMode::MidiSyncArduinoboy:
			if (message.getStatusMsg() == fw::MidiMessage::StatusMessage::NoteOn) {
				switch (message.getNoteNumber()) {
				case 24: _arduinoboyPlaying = true; break;
				case 25: _arduinoboyPlaying = false; break;
				case 26: getRawState().tempoDivisor = 1; break;
				case 27: getRawState().tempoDivisor = 2; break;
				case 28: getRawState().tempoDivisor = 4; break;
				case 29: getRawState().tempoDivisor = 8; break;
				default:
					if (message.getNoteNumber() >= 30) {
						system.getIo()->input.serial.tryPush(TimedByte{
							.audioFrameOffset = message.offset,
							.byte = (uint8)(message.getNoteNumber() - 30)
						});
					}
				}
			}

			break;
		case LsdjSyncMode::MidiMap:
			switch (message.getStatusMsg()) {
			case fw::MidiMessage::StatusMessage::NoteOn:
			{
				int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
				if (rowIdx != -1) {
					system.getIo()->input.serial.tryPush(TimedByte{
						.audioFrameOffset = message.offset,
						.byte = (uint8)rowIdx
					});

					_lastRow = rowIdx;
				}

				break;
			}
			case fw::MidiMessage::StatusMessage::NoteOff:
			{
				int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
				if (rowIdx == _lastRow) {
					system.getIo()->input.serial.tryPush(TimedByte{
						.audioFrameOffset = message.offset,
						.byte = 0xFE
					});

					_lastRow = -1;
				}

				break;
			}
			}

			break;
		}
	}

	void ArduinoboyService::onMidiClock(System& system) {
		/*Lsdj& lsdj = plug->lsdj();
		if (_transportChanged && plug->midiSync() && !lsdj.found) {
			if (mTimeInfo.mTransportIsRunning) {
				plug->sendMidiByte(0, 0xFA);
			} else {
				plug->sendMidiByte(0, 0xFC);
			}
		}

		if (mTimeInfo.mTransportIsRunning) {
			if (lsdj.found) {
				switch (getRawState().syncMode) {
				case LsdjSyncMode::Midi:
					ProcessSync(plug, frameCount, 1, 0xF8);
					break;
				case LsdjSyncMode::MidiSyncArduinoboy:
					if (lsdj.arduinoboyPlaying) {
						ProcessSync(plug, frameCount, lsdj.tempoDivisor, 0xF8);
					}
					break;
				case LsdjSyncMode::MidiMap:
					ProcessSync(plug, frameCount, 1, 0xFF);
					break;
				}
			} else if (plug->midiSync()) {
				processSync(plug, frameCount, 1, 0xF8);
			}
		}*/
	}

	void ArduinoboyService::processSync(System& system, int32 sampleCount, int32 tempoDivisor, char value) {
		const int32 resolution = 24 / tempoDivisor;
		const f64 samplesPerMs = _timeInfo.sampleRate / 1000.0;
		const f64 beatLenMs = (60000.0 / _timeInfo.tempo);
		const f64 beatLenSamples = beatLenMs * samplesPerMs;
		const f64 beatLenSamples24 = beatLenSamples / resolution;

		const f64 ppq24 = _timeInfo.ppqPos * resolution;
		const f64 framePpqLen = (sampleCount / beatLenSamples) * resolution;

		const f64 nextPpq24 = ppq24 + framePpqLen;

		bool sync = false;
		int32 offset = 0;

		if (ppq24 == 0) {
			sync = true;
		} else if ((int32)ppq24 != (int32)nextPpq24) {
			f64 amount = ceil(ppq24) - ppq24;

			sync = true;
			offset = (int32)(beatLenSamples24 * amount);

			if (offset >= sampleCount) {
				//consoleLogLine(("Overshot: " + std::to_string(offset - sampleCount)));
				offset = sampleCount - 1;
			}

			if (offset < 0) {
				offset = 0;
			}
		}

		if (sync) {
			system.getIo()->input.serial.tryPush(TimedByte{
				.audioFrameOffset = (uint32)offset,
				.byte = (uint8)value
			});
		}
	}
}
