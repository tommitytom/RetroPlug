#include "LsdjAudioHooks.h"

#include "audio/PpqUtil.h"
#include "core/SystemTypes.h"
#include "lsdj/LsdjSettings.h"
#include "lsdj/LsdjComponents.h"
#include "sameboy/SameBoyUtil.h"

namespace rp {
	const uint8 startOctave = 36;
	const uint8 KEYBOARD_NODE_START = 48;

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

	const uint8_t keyboardTblUp = 0x0B;
	const uint8_t keyboardTblDn = 0x03;

	namespace {
		int32 midiMapRowNumber(int32 channel, int32 note) {
			if (channel == 0) {
				return note;
			}

			if (channel == 1) {
				return note + 128;
			}

			return -1;
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

		void processSync(SameBoyStateComponent& state, const orb::TimeInfo& timeInfo, int32 tempoDivisor, uint8 value) {
			auto& serial = state.state->io->input.serial;
			orb::PpqUtil::eachTick(timeInfo, 24 / tempoDivisor, [&serial, value](uint32 ppq, uint32 offset) {
				sendSerialByte(serial, value, offset);
			});
		}
	}

	void LsdjAudioHooks::onTransportChange(entt::registry& registry, entt::entity entity, bool running) const {
		const LsdjAudioComponent& lsdj = registry.get<LsdjAudioComponent>(entity);
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);

		if (lsdj.syncMode == LsdjSyncMode::MidiSyncArduinoboy) {
			auto& serial = state.state->io->input.serial;

			if (running) {
				sendSerialByte(serial, 0, 0xFA);
			} else {
				sendSerialByte(serial, 0, 0xFC);
			}
		}

		if (lsdj.autoPlay) {
			// TODO: Determine if lsdj is already playing
			state.state->io->input.buttons.push_back(orb::StreamButtonPress{ orb::ButtonType::Start, true, 30 });
		}
	}

	void LsdjAudioHooks::onTransportUpdate(entt::registry& registry, entt::entity entity, const orb::TimeInfo& timeInfo) const {
		if (timeInfo.transportIsRunning) {
			const LsdjAudioComponent& lsdj = registry.get<LsdjAudioComponent>(entity);
			LsdjAudioStateComponent& lsdjState = registry.get<LsdjAudioStateComponent>(entity);
			SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);

			switch (lsdj.syncMode) {
			case LsdjSyncMode::MidiSync:
				processSync(state, timeInfo, 1, 0xF8);
				break;
			case LsdjSyncMode::MidiSyncArduinoboy:
				if (lsdjState.arduinoboyPlaying) {
					processSync(state, timeInfo, lsdjState.tempoDivisor, 0xF8);
				}
				break;
			case LsdjSyncMode::MidiMap:
				processSync(state, timeInfo, 1, 0xFF);
				break;
			}
		}
	}

	void LsdjAudioHooks::onMidi(entt::registry& registry, entt::entity entity, const orb::MidiMessage& message) const {
		const LsdjAudioComponent& lsdj = registry.get<LsdjAudioComponent>(entity);
		LsdjAudioStateComponent& lsdjState = registry.get<LsdjAudioStateComponent>(entity);
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		assert(state.state->io);
		auto& serial = state.state->io->input.serial;

		switch (lsdj.syncMode) {
		case LsdjSyncMode::KeyboardMidi:
			if (message.getStatusMsg() == orb::MidiMessage::StatusMessage::NoteOn) {
				uint8 note = (uint8)message.getNoteNumber();

				if (note >= KEYBOARD_NODE_START) {
					note -= KEYBOARD_NODE_START;

					lsdjState.keyboardOctave = changeOctave(serial, note / 12, lsdjState.keyboardOctave);

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
			if (message.getStatusMsg() == orb::MidiMessage::StatusMessage::NoteOn) {
				switch (message.getNoteNumber()) {
				case 24: lsdjState.arduinoboyPlaying = true; break;
				case 25: lsdjState.arduinoboyPlaying = false; break;
				case 26: lsdjState.tempoDivisor = 1; break;
				case 27: lsdjState.tempoDivisor = 2; break;
				case 28: lsdjState.tempoDivisor = 4; break;
				case 29: lsdjState.tempoDivisor = 8; break;
				default:
					if (message.getNoteNumber() >= 30) {
						sendSerialByte(serial, (uint8)(message.getNoteNumber() - 30), message.offset);
					}
				}
			}

			break;
		case LsdjSyncMode::MidiMap:
			switch (message.getStatusMsg()) {
			case orb::MidiMessage::StatusMessage::NoteOn:
			{
				int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
				if (rowIdx != -1) {
					sendSerialByte(serial, (uint8)rowIdx, message.offset);
					lsdjState.lastRow = rowIdx;
				}

				break;
			}
			case orb::MidiMessage::StatusMessage::NoteOff:
			{
				int32 rowIdx = midiMapRowNumber(message.getChannel(), message.getNoteNumber());
				if (rowIdx == lsdjState.lastRow) {
					sendSerialByte(serial, 0xFE, message.offset);
					lsdjState.lastRow = -1;
				}

				break;
			}
			}

			break;
		case LsdjSyncMode::MidiPassthrough:
		{
			serial.tryPush(TimedByte{ .byte = message.status, .audioFrameOffset = message.offset });
			serial.tryPush(TimedByte{ .byte = message.data1, .audioFrameOffset = message.offset });
			serial.tryPush(TimedByte{ .byte = message.data2, .audioFrameOffset = message.offset });
		}
		}
	}

	void LsdjAudioHooks::onMidiClock(entt::registry& registry, entt::entity entity) const {
		/*
		Lsdj& lsdj = plug->lsdj();
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
				case LsdjSyncMode::MidiSync:
					processSync(plug, frameCount, 1, 0xF8);
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
		}
		*/
	}
}
