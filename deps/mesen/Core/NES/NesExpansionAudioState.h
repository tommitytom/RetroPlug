#pragma once
#include "pch.h"

// Decoded live state of the active NES expansion audio chip (VRC6 / VRC7 /
// Sunsoft-5B / Namco-163), surfaced through BaseMapper::GetExpansionAudioState()
// so the host can read per-channel audibility/pitch without audio analysis
// (the expansion-audio analogue of NesApu::GetState()). Mesen-neutral: the host
// maps this to its own rp::ExpansionAudioState.
//
// `volume` is normalised to 0 (silent) .. 15 (loudest) across all chips so
// "silent means low volume" reads uniformly; `period` stays chip-native.
//
// `Frequency` is the DECODED output pitch in Hz, derived from each chip's own
// clocking model (the expansion-audio analogue of NesApu ApuSquareState.Frequency).
// Like the APU field it is computed from the pitch register regardless of whether
// the voice is audible, so a caller gates "is a note sounding" on Enabled / Volume
// and reads Frequency for "what pitch"; it is 0 only when the pitch is undefined
// (e.g. a zero timer/fnum). `WaveLength` and `ActiveChannels` are N163-only cross-
// check fields (the two terms besides the freq register that set N163 pitch), 0 for
// the other chips.

struct NesExpansionAudioChannel
{
	bool     Enabled        = false; // channel enabled / keyed on
	uint8_t  Volume         = 0;     // normalised 0=silent .. 15=loudest
	uint32_t OutputLevel    = 0;     // live decoded output magnitude (0 = silent right now)
	uint32_t Period         = 0;     // chip-native pitch reg (VRC6/5B timer, N163 18-bit, VRC7 fnum)
	double   Frequency      = 0.0;   // decoded output pitch in Hz (0 when undefined)
	uint8_t  Block          = 0;     // VRC7 octave 0-7 (0 for other chips)
	uint8_t  Duty           = 0;     // VRC6 pulse duty 0-7 (0 for other chips)
	bool     ConstantOutput = false; // VRC6 pulse "ignore duty" mode bit -> DC, no tone
	uint8_t  Instrument     = 0;     // VRC7 patch 0=custom,1-15 ROM (0 for other chips)
	uint16_t WaveLength     = 0;     // N163: active wave length in samples (0 for other chips)
	uint8_t  ActiveChannels = 0;     // N163: enabled channel count 1-8 (0 for other chips)
};

struct NesExpansionAudioState
{
	// "none" | "vrc6" | "vrc7" | "s5b" | "n163"
	string chip = "none";
	vector<NesExpansionAudioChannel> channels;
};
