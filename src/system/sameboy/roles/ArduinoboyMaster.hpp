#pragma once

#include <cstdint>
#include <vector>

#include "transport/MidiTypes.hpp"

// Decodes the byte stream LSDJ emits on its serial port when SYNC is set to
// MI.OUT in the PROJECT screen (Arduinoboy "master" mode). Each byte from
// LSDJ encodes either a clock tick, a song-start/stop, or per-channel note
// data. The hardware Arduinoboy firmware translates these bytes into real
// MIDI events; this class does the same job in software, emitting
// `::MidiEvent`s that the caller pushes to the per-system `midiOut_` queue.
//
// Protocol reference: Arduinoboy firmware, `Mode_LSDJ_Master.ino` at
// https://github.com/trash80/Arduinoboy. The decoder is isolated here (no
// SameBoySystem dependency) so it can be exercised from unit tests against
// recorded byte fixtures, then trusted as a black box from `LsdjSyncRole`.
//
// Encoding (verbatim from the firmware comments):
//
//   value < 0x7F → clock-driven note data for a song channel. The lower nibble
//   carries channel (0..3 = pulse1/pulse2/wave/noise); the upper nibble plus
//   a follow-up byte form a 7-bit note number for that channel. (This is the
//   only multi-byte command in the protocol — one tag byte + one note byte.)
//
//   value 0x7F .. 0xFE → "tagged" commands:
//     0x7F → instrument table command (rarely useful from a DAW)
//     0xB0..0xBF → channel mute toggles
//     0xC0..0xCF → channel pan controls
//   Clock + transport are handled separately via 0xFF, 0xFA, 0xFC bytes.
//
//   Important: the simplest-and-correct implementation for step 09 forwards
//   the documented transport bytes (0xFA start, 0xFC stop, 0xF8 clock when
//   present in the LSDJ output) directly as host-bound MIDI. The richer
//   per-channel decoding can land in a follow-up if needed.
class ArduinoboyMaster {
public:
    // Feed one serial-out byte from LSDJ. Pushes decoded `::MidiEvent`s into
    // `out`. The caller (typically `LsdjSyncRole`) appends those events to
    // the per-system `midiOut_` queue.
    void feed(std::uint8_t byte, std::vector<::MidiEvent>& out);

    // Reset all decoder state. Call on mode-flip or system reset.
    void reset();

private:
    // Some commands are two bytes (tag + note). When we've consumed a tag we
    // wait for the second byte here.
    enum class Pending : std::uint8_t {
        None  = 0,
        Note  = 1,  // expecting note number after a per-channel tag byte
    };

    Pending      pending_     = Pending::None;
    std::uint8_t pendingChan_ = 0;
};
