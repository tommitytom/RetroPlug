#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "system/mesen/MesenAudioDevice.hpp"
#include "util/ExpSmoother.hpp"


/** Drain the audio ring into `outs` through a smoothed gain, accumulating rather than overwriting.
 *
 *  All three Mesen systems (and SameBoy) carried this loop verbatim, and all four discarded what `drain`
 *  returns - which is how many frames it ACTUALLY wrote, not how many were asked for. `scratch` is only
 *  resized when it grows, so a short ring left the tail of the block holding the PREVIOUS block's samples
 *  and summed those into the output: a stale echo rather than the silence the caller expects. Zeroing the
 *  unfilled tail is the whole fix, and it only costs anything in the case that was broken.
 *
 *  Reachable today only on SMS, which is the one core whose step loop can give up before the target
 *  (kInstructionBudget); NES and GBA loop until the ring is full. It becomes reachable on those two the
 *  moment they gain a bound, which is exactly what this refactor is heading towards. */
inline void sumStereoWithGain(MesenAudioDevice& device,
                              std::vector<float>& scratch,
                              float* const*       outs,
                              std::uint32_t       frames,
                              ExpSmoother&        gain) {
    const std::size_t needed = std::size_t(frames) * 2;
    if (scratch.size() < needed) scratch.assign(needed, 0.0f);

    const std::uint32_t got = device.drain(scratch.data(), frames);
    // The ring came up short: blank the rest rather than summing whatever was left there last block.
    if (got < frames) std::fill(scratch.begin() + std::size_t(got) * 2, scratch.begin() + needed, 0.0f);

    float* outL = outs[0];
    float* outR = outs[1];
    for (std::uint32_t i = 0; i < frames; ++i) {
        const float g = gain.next();
        outL[i] += scratch[std::size_t(i) * 2 + 0] * g;
        outR[i] += scratch[std::size_t(i) * 2 + 1] * g;
    }
}

