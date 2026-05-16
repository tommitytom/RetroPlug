#pragma once

#include <cmath>
#include <cstdint>

#include "system/SystemTypes.hpp"

// Walks the PPQ boundaries that fall inside this block at `resolution` 
// ticks per quarter note (24 for MIDI clock), calling 
// `fn(ppqTickIndex, sampleOffsetInBlock)` for each. No-op when the 
// transport is not running.
namespace PpqUtil {

template <typename Fn>
inline void eachTick(const AudioBlockInfo& info, std::uint32_t resolution, Fn&& fn) {
    if (!info.transportPlaying || info.frames == 0 || resolution == 0 || info.tempo <= 0.0)
        return;

    const double beatLenSamples   = info.sampleRate * 60.0 / info.tempo;
    const double beatLenSamplesRes = beatLenSamples / static_cast<double>(resolution);
    const double ppqRes           = info.ppqPosBlockStart * static_cast<double>(resolution);
    const double framePpqLen      = (static_cast<double>(info.frames) / beatLenSamples)
                                  * static_cast<double>(resolution);
    const double framePpqEnd      = ppqRes + framePpqLen;

    double lastPpq = ppqRes;
    double nextPpq = std::ceil(ppqRes);
    double offset  = 0.0;

    while (nextPpq < framePpqEnd) {
        offset += beatLenSamplesRes * (nextPpq - lastPpq);
        if (offset >= static_cast<double>(info.frames))
            offset = static_cast<double>(info.frames) - 1.0;
        if (offset < 0.0)
            offset = 0.0;

        fn(static_cast<std::uint32_t>(nextPpq), static_cast<std::uint32_t>(offset));

        lastPpq = nextPpq;
        nextPpq += 1.0;
    }
}

} // namespace PpqUtil
