#pragma once

#include <cmath>
#include <cstdint>

#include "system/SystemTypes.hpp"

// Walks the PPQ boundaries that fall inside this block at `resolution`
// ticks per quarter note (24 for MIDI clock), calling
// `fn(ppqTickIndex, sampleOffsetInBlock)` for each. No-op when the
// transport is not running.
//
// `nextTick` is the caller-owned index of the next tick to emit; it persists
// across blocks and is advanced as ticks fire. This statefulness is what makes
// the clock exact over long runs: the per-block end position is computed from
// frames/tempo while the host supplies each block's start position, and those
// two floating-point values disagree by an epsilon at block boundaries. A
// stateless `ceil(blockStart)`-per-block scheme therefore emits a boundary tick
// twice (LSDj races ahead) or zero times (LSDj falls behind) whenever a tick
// lands on a block edge — a measurable ~21 ms (one tick) of MidiSync drift that
// accumulates over an hour. Carrying `nextTick` forward guarantees every tick
// fires exactly once, in order, regardless of where block edges fall.
namespace PpqUtil {

template <typename Fn>
inline void eachTick(const AudioBlockInfo& info, std::uint32_t resolution,
                     std::int64_t& nextTick, Fn&& fn) {
    if (!info.transportPlaying || info.frames == 0 || resolution == 0 || info.tempo <= 0.0)
        return;

    const double beatLenSamples    = info.sampleRate * 60.0 / info.tempo;
    const double beatLenSamplesRes = beatLenSamples / static_cast<double>(resolution);
    const double ppqRes            = info.ppqPosBlockStart * static_cast<double>(resolution);
    const double framePpqLen       = (static_cast<double>(info.frames) / beatLenSamples)
                                   * static_cast<double>(resolution);
    const double framePpqEnd       = ppqRes + framePpqLen;

    // Resync on a transport jump (seek / loop / start). The ±1-tick window is
    // far wider than block-boundary FP jitter (< 1e-6 tick) but far narrower
    // than any real jump, so normal playback never resyncs — preserving the
    // contiguous counter that fixes the drift.
    if (static_cast<double>(nextTick) < ppqRes - 1.0 ||
        static_cast<double>(nextTick) > framePpqEnd + 1.0)
        nextTick = static_cast<std::int64_t>(std::ceil(ppqRes));

    while (static_cast<double>(nextTick) < framePpqEnd) {
        double offset = beatLenSamplesRes * (static_cast<double>(nextTick) - ppqRes);
        if (offset < 0.0) offset = 0.0;
        if (offset >= static_cast<double>(info.frames))
            offset = static_cast<double>(info.frames) - 1.0;

        fn(static_cast<std::uint32_t>(nextTick), static_cast<std::uint32_t>(offset));
        ++nextTick;
    }
}

} // namespace PpqUtil
