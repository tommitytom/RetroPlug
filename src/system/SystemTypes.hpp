#pragma once

#include <cstdint>

using SystemId = std::uint32_t;

enum class SystemKind : std::uint32_t {
    SameBoy = 0,
    Mesen   = 1,  // placeholder; not implemented yet
};

// Per-block context handed to SystemBase::onProcess. Audio thread only.
struct AudioBlockInfo {
    std::uint32_t frames;             // number of sample frames in this block
    double        sampleRate;         // host sample rate (Hz)
    double        tempo            = 120.0;  // BPM; populated from host (DPF bbt.beatsPerMinute) or CLI sim
    double        ppqPosBlockStart = 0.0;    // continuous beat position at the first sample of this block
    bool          transportPlaying = false;  // host transport running
};
