#pragma once

#include <cassert>

#include "foundation/Types.h"

namespace fw {
	struct MidiMessage {
		uint32 offset = 0;
		uint8 status = 0;
		uint8 data1 = 0;
		uint8 data2 = 0;

		enum class StatusMessage {
			None = 0,
			NoteOff = 8,
			NoteOn = 9,
			PolyAftertouch = 10,
			ControlChange = 11,
			ProgramChange = 12,
			ChannelAftertouch = 13,
			PitchWheel = 14
		};

		enum class ControlChangeMessage {
			NoCC = -1,
			ModWheel = 1,
			BreathController = 2,
			Undefined003 = 3,
			FootController = 4,
			PortamentoTime = 5,
			ChannelVolume = 7,
			Balance = 8,
			Undefined009 = 9,
			Pan = 10,
			ExpressionController = 11,
			EffectControl1 = 12,
			EffectControl2 = 13,
			Undefined014 = 14,
			Undefined015 = 15,
			GeneralPurposeController1 = 16,
			GeneralPurposeController2 = 17,
			GeneralPurposeController3 = 18,
			GeneralPurposeController4 = 19,
			Undefined020 = 20,
			Undefined021 = 21,
			Undefined022 = 22,
			Undefined023 = 23,
			Undefined024 = 24,
			Undefined025 = 25,
			Undefined026 = 26,
			Undefined027 = 27,
			Undefined028 = 28,
			Undefined029 = 29,
			Undefined030 = 30,
			Undefined031 = 31,
			SustainOnOff = 64,
			PortamentoOnOff = 65,
			SustenutoOnOff = 66,
			SoftPedalOnOff = 67,
			LegatoOnOff = 68,
			Hold2OnOff = 69,
			SoundVariation = 70,
			Resonance = 71,
			ReleaseTime = 72,
			AttacTime = 73,
			CutoffFrequency = 74,
			DecayTime = 75,
			VibratoRate = 76,
			VibratoDepth = 77,
			VibratoDelay = 78,
			SoundControllerUndefined = 79,
			Undefined085 = 85,
			Undefined086 = 86,
			Undefined087 = 87,
			Undefined088 = 88,
			Undefined089 = 89,
			Undefined090 = 90,
			TremoloDepth = 92,
			ChorusDepth = 93,
			PhaserDepth = 95,
			Undefined102 = 102,
			Undefined103 = 103,
			Undefined104 = 104,
			Undefined105 = 105,
			Undefined106 = 106,
			Undefined107 = 107,
			Undefined108 = 108,
			Undefined109 = 109,
			Undefined110 = 110,
			Undefined111 = 111,
			Undefined112 = 112,
			Undefined113 = 113,
			Undefined114 = 114,
			Undefined115 = 115,
			Undefined116 = 116,
			Undefined117 = 117,
			Undefined118 = 118,
			Undefined119 = 119,
			AllNotesOff = 123
		};

		uint32 getChannel() const {
			return status & 0x0F;
		}

		void setChannel(uint32 channel) {
			assert(channel < 16);
			status &= 0xF0;
			status |= ((uint8)channel & 0x0F);
		}

		StatusMessage getStatusMsg() const {
			uint32 e = status >> 4;
			if (e < (uint32)StatusMessage::NoteOff || e >(uint32)StatusMessage::PitchWheel) {
				return StatusMessage::None;
			}

			return (StatusMessage)e;
		}

		int32 getNoteNumber() const {
			switch (getStatusMsg()) {
			case StatusMessage::NoteOn:
			case StatusMessage::NoteOff:
			case StatusMessage::PolyAftertouch:
				return data1;
			default:
				return -1;
			}
		}

		int32 getVelocity() const {
			switch (getStatusMsg()) {
			case StatusMessage::NoteOn:
			case StatusMessage::NoteOff:
				return data2;
			default:
				return -1;
			}
		}

		int32 getPolyAfterTouch() const {
			switch (getStatusMsg()) {
			case StatusMessage::PolyAftertouch:
				return data2;
			default:
				return -1;
			}
		}

		int32 getChannelAfterTouch() const {
			switch (getStatusMsg()) {
			case StatusMessage::ChannelAftertouch:
				return data1;
			default:
				return -1;
			}
		}

		int32 getProgram() const {
			if (getStatusMsg() == StatusMessage::ProgramChange) {
				return data1;
			}

			return -1;
		}

		f64 getPitchWheel() const {
			if (getStatusMsg() == StatusMessage::PitchWheel) {
				int32 val = (data2 << 7) + data1;
				return (f64)(val - 8192) / 8192.0;
			}

			return 0.0;
		}

		ControlChangeMessage getControlChangeIdx() const {
			return (ControlChangeMessage)data1;
		}

		f64 controlChange(ControlChangeMessage idx) const {
			if (getStatusMsg() == StatusMessage::ControlChange && getControlChangeIdx() == idx) {
				return (f64)data2 / 127.0;
			}

			return -1.0;
		}

		void clear() {
			offset = 0;
			status = 0;
			data1 = 0;
			data2 = 0;
		}

		static bool controlChangeOnOff(f64 msgValue) {
			return (msgValue >= 0.5);
		}
	};
}
