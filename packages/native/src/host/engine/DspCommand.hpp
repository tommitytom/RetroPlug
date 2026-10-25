#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A control-thread → audio-thread command for the engine, carried on the GraphHost's one ring as an
// opaque HostCommand (docs/modular-graph-phase-b1-design.md section 5.1), in push order with the plan
// swaps. POD / trivially copyable so it rides the ring as bytes. The heavy payloads (rare,
// user-initiated) cross as raw OWNING pointers: the audio thread applies then `delete`s them, the
// accepted non-RT-on-rare-op pattern. StageMidi is inline for a channel message (fits in 4 bytes); a
// longer one (SysEx, or several messages staged as one run) crosses as an owning `ext` vector, the same
// rare-op pattern. Structure never comes this way: TS sends it as graph documents.
struct DspCommand {
    enum class Kind : std::uint8_t {
        None = 0, SetSystems = 1, LoadKernel = 2, StageMidi = 3,
        SetBpm = 7, SetTransport = 8, SetConfigField = 9, PressButton = 10,
        SetPpq = 12, StageControllerMidi = 13, WriteRam = 14,
        StageSystemMidi = 15,
        // The NES tap arming every structural change does (docs/modular-graph-phase-b1-design.md section
        // 3.2): restore whatever is armed, then arm `id` to `mode` if `arm`. Only ever the trailer of a
        // setGraph's swap, so it lands with the plan it was made for.
        SyncTap = 16,
    };

    Kind kind = Kind::None;
    union {
        struct { std::string* json; } setSystems;                 // owning; audio thread deletes
        struct { std::vector<std::uint8_t>* bytecode; } loadKernel; // owning; audio thread deletes
        // `len` bytes inline when the message fits; else `ext` OWNS the whole message (audio thread deletes)
        // and `len` is 0. Also carries StageControllerMidi, which never uses `ext` (a pad press is 3 bytes).
        struct { std::uint8_t data[4]; std::uint8_t len; std::vector<std::uint8_t>* ext; } stageMidi;
        struct { double value; } setBpm;                          // transport as a queued op
        struct { bool value; } setTransport;
        struct { std::uint32_t id; std::uint8_t field; double value; } setConfigField; // live setting → core
        struct { std::uint32_t id; std::uint8_t button; bool down; } pressButton; // joypad transition → core
        struct { double value; } setPpq;                         // host playhead jump (locate)
        // Host poke into a core's work RAM. Owning payload (a song block is ~7 KB, far past what the
        // union can hold inline), freed by the audio thread after applying - the same rare-op pattern
        // setSystems and loadKernel use.
        struct { std::uint32_t id; std::uint32_t offset; std::vector<std::uint8_t>* bytes; } writeRam;
        // MIDI aimed at ONE system, delivered straight to its ingress and NEVER through the routing kernel.
        // That is the whole point: a menu knob has to reach the cart whose menu it is, whatever the project's
        // MUSICAL routing is doing with channels (sendToAll would hit every instance; midiChannelToInstance
        // rewrites the channel out from under it). `len` bytes inline when the message fits, else `ext` OWNS
        // the whole message (audio thread deletes) and `len` is 0 - the same shape as stageMidi, because a
        // settings message is a SysEx and a rig block does not fit in four bytes.
        struct { std::uint32_t id; std::uint8_t data[4]; std::uint8_t len; std::vector<std::uint8_t>* ext; } stageSystemMidi;
        struct { bool arm; std::uint8_t mode; std::uint32_t id; } syncTap;  // what it arms after the restore
    };

    DspCommand() : kind(Kind::None), stageMidi{{0, 0, 0, 0}, 0, nullptr} {}
};
