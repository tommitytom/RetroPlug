#include "system/sameboy/roles/ArduinoboyMaster.hpp"

namespace {

constexpr std::uint8_t kMidiClock = 0xF8;
constexpr std::uint8_t kMidiStart = 0xFA;
constexpr std::uint8_t kMidiStop  = 0xFC;
constexpr std::uint8_t kNoteOnBase = 0x90;

// Push a one-byte system real-time message.
inline void pushRealtime(std::vector<::MidiEvent>& out, std::uint8_t status) {
    ::MidiEvent ev{};
    ev.frame   = 0;
    ev.size    = 1;
    ev.data[0] = status;
    out.push_back(ev);
}

// Push a NoteOn on the given channel (0..3 mapped to MIDI ch 1..4).
inline void pushNoteOn(std::vector<::MidiEvent>& out,
                       std::uint8_t channel0123,
                       std::uint8_t note) {
    ::MidiEvent ev{};
    ev.frame   = 0;
    ev.size    = 3;
    ev.data[0] = static_cast<std::uint8_t>(kNoteOnBase | (channel0123 & 0x0F));
    ev.data[1] = note & 0x7F;
    ev.data[2] = 0x7F; // velocity full
    out.push_back(ev);
}

} // namespace

void ArduinoboyMaster::reset() {
    pending_     = Pending::None;
    pendingChan_ = 0;
}

void ArduinoboyMaster::feed(std::uint8_t byte, std::vector<::MidiEvent>& out) {
    // Complete a pending two-byte note command first.
    if (pending_ == Pending::Note) {
        pushNoteOn(out, pendingChan_, byte);
        pending_ = Pending::None;
        return;
    }

    // System real-time bytes pass straight through.
    switch (byte) {
        case kMidiClock: pushRealtime(out, kMidiClock); return;
        case kMidiStart: pushRealtime(out, kMidiStart); return;
        case kMidiStop:  pushRealtime(out, kMidiStop);  return;
        default: break;
    }

    // Per-channel note tag: lower nibble = channel (0..3), upper nibble
    // signals the follow-up byte carries the note number.
    if ((byte & 0xF0) == 0x00 && (byte & 0x0F) < 4) {
        pendingChan_ = byte & 0x0F;
        pending_     = Pending::Note;
        return;
    }

    // Everything else (mute/pan/instrument-table commands in the documented
    // protocol) is currently dropped. Step 09 prioritizes the clock/transport
    // path which is what DAW users primarily need from MI.OUT; richer
    // command coverage is a future enhancement.
}
