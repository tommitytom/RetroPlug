#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SystemBase;  // pointer-only — keeps DspCommand trivially copyable and the header light

// A control-thread → audio-thread command for the DSP kernel, carried by an
// SpscRing<DspCommand, N>. POD / trivially copy-assignable so it rides the ring without heap
// churn. The heavy payloads (rare, user-initiated) cross as raw OWNING pointers — the audio thread
// applies then `delete`s / hands them off, the accepted non-RT-on-rare-op pattern from PluginDSP's
// LoadProject / AddSystem. StageMidi is fully inline (a MIDI message fits in 4 bytes).
//
// The lifecycle kinds mirror production's Command: the control thread builds + onActivate's the
// SameBoySystem off-thread and ships the raw pointer here; the audio thread does an alloc-free
// pointer swap into the pre-reserved Project. A displaced/removed system is handed back to the
// control thread through the DspEvent ring for off-thread delete.
struct DspCommand {
    enum class Kind : std::uint8_t {
        None = 0, SetSystems = 1, LoadKernel = 2, StageMidi = 3,
        AddSystem = 4, ReplaceSystem = 5, RemoveSystem = 6,
        SetBpm = 7, SetTransport = 8, SetConfigField = 9, PressButton = 10,
        SetAudioRouting = 11, SetPpq = 12, WriteRam = 13,
    };

    Kind kind = Kind::None;
    union {
        struct { std::string* json; } setSystems;                 // owning; audio thread deletes
        struct { std::vector<std::uint8_t>* bytecode; } loadKernel; // owning; audio thread deletes
        struct { std::uint8_t data[4]; std::uint8_t len; } stageMidi;
        struct { SystemBase* sys; } addSystem;                    // owning; adopted into the Project
        struct { SystemBase* sys; std::uint32_t id; } replaceSystem; // owning; swapped for id
        struct { std::uint32_t id; } removeSystem;
        struct { double value; } setBpm;                          // transport as a queued op
        struct { bool value; } setTransport;
        struct { std::uint32_t id; std::uint8_t field; double value; } setConfigField; // live setting → core
        struct { std::uint32_t id; std::uint8_t button; bool down; } pressButton; // joypad transition → core
        struct { std::uint8_t mode; } setAudioRouting;            // project output-pair placement
        struct { double value; } setPpq;                         // host playhead jump (locate)
        // Host poke into a core's work RAM. Owning payload (a song block is ~7 KB, far past what the
        // union can hold inline), freed by the audio thread after applying - the same rare-op pattern
        // setSystems and loadKernel use.
        struct { std::uint32_t id; std::uint32_t offset; std::vector<std::uint8_t>* bytes; } writeRam;
    };

    DspCommand() : kind(Kind::None), stageMidi{{0, 0, 0, 0}, 0} {}
};
