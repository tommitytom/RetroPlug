#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A control-thread → audio-thread command for the greenfield DSP kernel, carried by an
// SpscRing<DspCommand, N>. POD / trivially copy-assignable so it rides the ring without heap
// churn. The two structural commands (rare, user-initiated) carry a heap payload as a raw OWNING
// pointer — the audio thread applies it then `delete`s it, the accepted non-RT-on-rare-op pattern
// from PluginDSP's LoadProject. StageMidi is fully inline (a MIDI message fits in 4 bytes).
struct DspCommand {
    enum class Kind : std::uint8_t { None = 0, SetSystems = 1, LoadKernel = 2, StageMidi = 3 };

    Kind kind = Kind::None;
    union {
        struct { std::string* json; } setSystems;                 // owning; audio thread deletes
        struct { std::vector<std::uint8_t>* bytecode; } loadKernel; // owning; audio thread deletes
        struct { std::uint8_t data[4]; std::uint8_t len; } stageMidi;
    };

    DspCommand() : kind(Kind::None), stageMidi{{0, 0, 0, 0}, 0} {}
};
