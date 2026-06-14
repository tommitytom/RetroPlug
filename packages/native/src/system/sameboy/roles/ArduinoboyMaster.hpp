#pragma once

#include <cstdint>
#include <vector>

#include "transport/MidiTypes.hpp"

// TODO: Fully test and reassess documentation for Arduinoboy.

// Decodes the byte stream LSDJ emits on its serial port when SYNC is set to
// MI.OUT in the PROJECT screen (Arduinoboy "Mode_LSDJ_Midiout" mode).
//
// Protocol — verbatim from the Arduinoboy firmware at
// https://github.com/trash80/Arduinoboy/blob/master/Arduinoboy/Mode_LSDJ_Midiout.ino
//
//   Value bytes (0x00..0x6F): completes a previously-seen command byte.
//   Command bytes (0x70..0x7C): m = byte - 0x70. The next byte is the value:
//     m < 4         → NoteOn  channel m         (value=0 → NoteOff)
//     4 <= m < 8    → CC      channel m-4       (default mapping; the real
//                                                Arduinoboy supports several
//                                                CC-encoding modes that we
//                                                do NOT emulate here)
//     8 <= m < 0xC  → PC      channel m-8
//   Realtime commands (single-byte):
//     0x7D → transport start (emit 0xFA)
//     0x7E → transport stop  (emit 0xFC)
//     0x7F → clock tick      (emit 0xF8)
//
// Bytes with the high bit set (>= 0x80) are not part of the MI.OUT protocol
// per the firmware (the firmware's `default` case effectively ignores them
// because `byte - 0x70` lands outside the 0..11 command-id range). The
// decoder treats them as "ignore" for safety. The CLI's per-system raw
// `_serial_sys<N>.txt` log captures them anyway for diagnosis.
//
// LSDJ's effect commands that drive this protocol (placed in note/table
// cells in the LSDJ song editor):
//   Nxx — sends a NoteOn absolute to xx (N00 = NoteOff)
//   Qxx — sends a NoteOn relative to the channel's current pitch
//   Xxx — sends a CC
//   Yxx — sends a Program Change
// All four route through this byte protocol; the receiver doesn't see N/Q/X/Y
// distinctly — N and Q both end up as a NoteOn command.
class ArduinoboyMaster {
public:
    // Feed one serial-out byte from LSDJ. Pushes decoded `::MidiEvent`s into
    // `out`. The caller (typically `LsdjSyncRole`) appends those events to
    // the per-system `midiOut_` queue.
    void feed(std::uint8_t byte, std::vector<::MidiEvent>& out);

    // Reset all decoder state. Call on mode-flip or system reset.
    void reset();

private:
    // True once we've seen a 0x70..0x7C command byte and are waiting for the
    // matching value byte. Stored separately rather than overloading
    // pendingCmd_ so an out-of-band realtime byte during the wait can't
    // confuse us.
    bool         pendingValueExpected_ = false;
    std::uint8_t pendingCmd_           = 0; // m = byte - 0x70 from the spec
};
