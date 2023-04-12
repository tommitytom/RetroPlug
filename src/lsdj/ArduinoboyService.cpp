#include "ArduinoboyService.h"

#include "foundation/Event.h"
#include "foundation/Input.h"
#include "foundation/MathUtil.h"
#include "Ps2Util.h"

namespace rp {
	const uint8 startOctave = 36;
	const uint8 noteStart = 48;

	const uint8 keyboardNoteMap[24] = { 0x1A,0x1B,0x22,0x23,0x21,0x2A,0x34,0x32,0x33,0x31,0x3B,0x3A,
										 0x15,0x1E,0x1D,0x26,0x24,0x2D,0x2E,0x2C,0x36,0x35,0x3D,0x3C };

	const uint8_t keyboardLowOctaveMap[12] = {
		  0x01, //Mute1
		  0x09, //Mute2
		  0x78, //Mute3
		  0x07, //Mute4
		  0x68, //Cursor Left
		  0x74, //Cursor Right
		  0x75, //Cursor Up
		  0x72, //Cursor Down
		  0x5A, //Enter
		  0x7A, //Table Up
		  0x7D, //Table Down
		  0x29  //Table Cue
	};

	const uint8_t keyboardOctDn = 0x05;
	const uint8_t keyboardOctUp = 0x06;

	const uint8_t keyboardInsDn = 0x04;
	const uint8_t keyboardInsUp = 0x0C;

	const uint8_t keyboardTblDn = 0x03;
	const uint8_t keyboardTblUp = 0x0B;

	const uint8_t keyboardTblCue = 0x29;

	const uint8_t keyboardMut1 = 0x01;
	const uint8_t keyboardMut2 = 0x09;
	const uint8_t keyboardMut3 = 0x78;
	const uint8_t keyboardMut4 = 0x07;

	const uint8_t keyboardCurL = 0x6B;
	const uint8_t keyboardCurR = 0x74;
	const uint8_t keyboardCurU = 0x75;
	const uint8_t keyboardCurD = 0x72;
	const uint8_t keyboardPgUp = 0x7D;
	const uint8_t keyboardPgDn = 0x7A;
	const uint8_t keyboardEntr = 0x5A;
	
	int32 midiMapRowNumber(int32 channel, int32 note) {
		if (channel == 0) {
			return note;
		}

		if (channel == 1) {
			return note + 128;
		}

		return -1;
	}

	void ArduinoboyService::onAfterLoad(System& system) {
		this->receive<fw::KeyEvent>([this, &system](const fw::KeyEvent& ev) {
			switch (getRawState().syncMode) {
			case LsdjSyncMode::Keyboard: {
				uint8_t scancodes[8];
				int count = 0;
				if (ev.down == true) {
					count = Ps2Util::getMakeCode(ev.key, scancodes, true);
				} else {
					count = Ps2Util::getBreakCode(ev.key, scancodes);
				}

				const f64 LSDJ_PS2_BYTE_DELAY = 5.0;

				if (count) {
					const f64 samplesPerMs = (f64)system.getSampleRate() / 1000.0;
					uint32 accum = (uint32)(samplesPerMs * LSDJ_PS2_BYTE_DELAY);

					//const std::string* vkname = VirtualKey::toString((VirtualKey::Enum)key.VK);
					//std::string vkn = (vkname ? *vkname : "Unknown");

					//std::cout << (down == true ? "KEY DOWN |" : "KEY UP   |");
					//std::cout << std::hex << " Char: " << key.utf8[0] << " | VK: " << vkn << " | PS/2: [ ";

					//std::stringstream lsdjCodes;
					//lsdjCodes << std::hex;

					uint32 offset = 0;
					for (int i = 0; i < count; ++i) {
						//std::cout << "0x" << (int)(uint8_t)scancodes[i] << (i < count - 1 ? ", " : " ");
						//lsdjCodes << "0x" << (int)(uint8_t)(MathUtil::reverse(scancodes[i]) >> 1) << (i < count - 1 ? ", " : " ");

						uint8 byteToSend = fw::MathUtil::reverse(scancodes[i]) >> 1;

						system.getIo()->input.serial.tryPush(TimedByte{
							.byte = byteToSend,
							.audioFrameOffset = offset,
						});

						offset += accum;
					}

					//std::cout << "] | LSDj: [ " << lsdjCodes.str() << "]" << std::endl;
				}

				break;
			}
			}
		});
	}

	void sendSerialByte(FixedQueue<TimedByte, 16>& target, uint8 byte, uint32 audioFrameOffset) {
		target.tryPush(TimedByte{ byte, audioFrameOffset });
	}

	uint8 changeOctave(FixedQueue<TimedByte, 16>& target, uint8 octave, uint8 currentOctave) {
		if (octave != currentOctave) {
			int diff = abs(((int)octave) - ((int)currentOctave));

			if (octave > currentOctave) {
				while (diff--) {
					sendSerialByte(target, keyboardOctUp, 0);
				}
			} else {
				while (diff--) {
					sendSerialByte(target, keyboardOctDn, 0);
				}
			}
		}

		return octave;
	}

	void ArduinoboyService::onTransportChange(System& system, bool running) {
		if (getRawState().autoPlay) {
			ButtonStream<8> presses;
			presses.presses[0] = StreamButtonPress{ ButtonType::Start, true, 30 };
			system.getIo()->input.buttons.push_back(presses);
		}
	}

	void ArduinoboyService::onMidi(System& system, const fw::MidiMessage& message) {
		auto& serial = system.getIo()->input.serial;

		switch (getRawState().syncMode) {
			case LsdjSyncMode::KeyboardMidi:
				if (message.getStatusMsg() == fw::MidiMessage::StatusMessage::NoteOn) {
					uint8 note = (uint8)message.getNoteNumber();
				
					if (message.getNoteNumber() >= noteStart) {
						note -= noteStart;
					
						_keyboardOctave = changeOctave(serial, note / 12, _keyboardOctave);

						if (note >= 0x3C) {
							// Use second row of keyboard keys
							note = (note % 12) + 0x0C;
						} else {
							note = (note % 12);
						}

						sendSerialByte(serial, keyboardNoteMap[note], message.offset);
					} else if (note >= startOctave) {
						note -= startOctave;
						uint8 command = keyboardLowOctaveMap[note];
					
						if (command == 0x68 || command == 0x72 || command == 0x74 || command == 0x75) {
							//cursor values need an "extended" pc keyboard mode message
							sendSerialByte(serial, 0xE0, message.offset);
						}
					
						sendSerialByte(serial, command, message.offset);
					}
				}
				break;
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
								sendSerialByte(serial, (uint8)(message.getNoteNumber() - 30), message.offset);
							}
						}
				}

				break;
			case LsdjSyncMode::MidiMap:
				switch (message.getStatusMsg()) {
					case fw::MidiMessage::StatusMessage::NoteOn: {
						int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
						if (rowIdx != -1) {
							sendSerialByte(serial, (uint8)rowIdx, message.offset);
							_lastRow = rowIdx;
						}

						break;
					}
					case fw::MidiMessage::StatusMessage::NoteOff: {
						int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
						if (rowIdx == _lastRow) {
							sendSerialByte(serial, 0xFE, message.offset);
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
			sendSerialByte(system.getIo()->input.serial, (uint8)value, (uint32)offset);
		}
	}
}
