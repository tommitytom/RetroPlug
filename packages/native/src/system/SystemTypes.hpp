#pragma once

#include <cstdint>
#include <string_view>

using SystemId = std::uint32_t;

enum class SystemKind : std::uint32_t {
    SameBoy  = 0,
    MesenNes = 1,  // NES, via the Mesen backend
    MesenGba = 2,  // GBA, via the Mesen backend
};

// One output stream a system can emit (SystemBase::channelLayout()). The default layout is a single
// stereo "Mix" stream (today's behaviour); a backend that can split its audio (e.g. a Game Boy's four
// APU channels) reports one entry per stream. `name` must be a stable string (a static literal or a
// backend-owned buffer). A stereo stream occupies two lanes, a mono stream one.
struct ChannelStream {
    std::string_view name;
    bool             stereo = true;
};

// Per-block context handed to SystemBase::onProcess. Audio thread only.
struct AudioBlockInfo {
    std::uint32_t frames;             // number of sample frames in this block
    double        sampleRate;         // host sample rate (Hz)
    double        tempo            = 120.0;  // BPM; populated from host (DPF bbt.beatsPerMinute) or CLI sim
    double        ppqPosBlockStart = 0.0;    // continuous beat position at the first sample of this block
    bool          transportPlaying = false;  // host transport running
};
