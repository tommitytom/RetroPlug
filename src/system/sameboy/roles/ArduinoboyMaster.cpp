#include "system/sameboy/roles/ArduinoboyMaster.hpp"

namespace {

constexpr std::uint8_t kRealtimeStart = 0x7D; // → 0xFA
constexpr std::uint8_t kRealtimeStop  = 0x7E; // → 0xFC
constexpr std::uint8_t kRealtimeClock = 0x7F; // → 0xF8

constexpr std::uint8_t kCmdRangeFirst = 0x70;
constexpr std::uint8_t kCmdRangeLast  = 0x7C; // inclusive — last "needs a value" command

constexpr std::uint8_t kMidiNoteOnBase  = 0x90;
constexpr std::uint8_t kMidiNoteOffBase = 0x80;
constexpr std::uint8_t kMidiCcBase      = 0xB0;
constexpr std::uint8_t kMidiPcBase      = 0xC0;

inline void pushRealtime(std::vector<::MidiEvent>& out, std::uint8_t status) {
    ::MidiEvent ev{};
    ev.frame   = 0;
    ev.size    = 1;
    ev.data[0] = status;
    out.push_back(ev);
}

inline void pushChannel3(std::vector<::MidiEvent>& out,
                         std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
    ::MidiEvent ev{};
    ev.frame   = 0;
    ev.size    = 3;
    ev.data[0] = status;
    ev.data[1] = d1 & 0x7F;
    ev.data[2] = d2 & 0x7F;
    out.push_back(ev);
}

inline void pushChannel2(std::vector<::MidiEvent>& out,
                         std::uint8_t status, std::uint8_t d1) {
    ::MidiEvent ev{};
    ev.frame   = 0;
    ev.size    = 2;
    ev.data[0] = status;
    ev.data[1] = d1 & 0x7F;
    out.push_back(ev);
}

// `m` is the 0..11 command identifier extracted from `byte - 0x70`.
// Channels are 0-indexed (GB channel 0..3 → MIDI channel bits 0..3).
void emitCommandValue(std::uint8_t m, std::uint8_t v,
                      std::vector<::MidiEvent>& out) {
    if (m < 4) {
        const std::uint8_t ch = m;
        if (v == 0) {
            // Arduinoboy firmware sends a NoteOff for the most-recently-played
            // note on this channel. Without that running state we emit a
            // NoteOff on note 0 — sufficient as a "channel quiet" signal for
            // downstream MIDI consumers and unambiguous in the log.
            pushChannel3(out, std::uint8_t(kMidiNoteOffBase | ch), 0, 0);
        } else {
            pushChannel3(out, std::uint8_t(kMidiNoteOnBase | ch), v, 0x7F);
        }
    } else if (m < 8) {
        const std::uint8_t ch = m - 4;
        // The firmware supports several CC-encoding modes (single CC,
        // seven scaled CCs, etc.). We pick the simplest: CC number = m,
        // value = v. This is a known simplification and is documented in the
        // header — refine here when the use case demands it.
        pushChannel3(out, std::uint8_t(kMidiCcBase | ch), m, v);
    } else if (m < 0x0C) {
        const std::uint8_t ch = m - 8;
        pushChannel2(out, std::uint8_t(kMidiPcBase | ch), v);
    }
    // m >= 0x0C: undefined per the firmware; drop.
}

} // namespace

void ArduinoboyMaster::reset() {
    pendingValueExpected_ = false;
    pendingCmd_           = 0;
}

void ArduinoboyMaster::feed(std::uint8_t byte, std::vector<::MidiEvent>& out) {
    // Bytes >= 0x80 are not part of the documented MI.OUT protocol; drop
    // them defensively. The raw `_serial_sys<N>.txt` log still records them
    // for diagnosis (those bytes typically show up in adjacent modes like
    // KEYBD's polling stream, not real MI.OUT output).
    if (byte >= 0x80) return;

    // Realtime commands are single-byte and orthogonal to the
    // command/value pairing — they can fire even while we're waiting on a
    // value byte without disturbing the wait.
    switch (byte) {
        case kRealtimeClock: pushRealtime(out, 0xF8); return;
        case kRealtimeStart: pushRealtime(out, 0xFA); return;
        case kRealtimeStop:  pushRealtime(out, 0xFC); return;
        default: break;
    }

    if (byte >= kCmdRangeFirst && byte <= kCmdRangeLast) {
        // Command byte — start a pending command/value pair.
        pendingCmd_           = byte - kCmdRangeFirst;
        pendingValueExpected_ = true;
        return;
    }

    // Value byte (0x00..0x6F). Only meaningful when a command is pending.
    if (pendingValueExpected_) {
        emitCommandValue(pendingCmd_, byte, out);
        pendingValueExpected_ = false;
        pendingCmd_           = 0;
    }
}
