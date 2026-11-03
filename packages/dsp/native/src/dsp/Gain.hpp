#pragma once

// The gain kernel and the whole-buffer peak the kit path normalises with.
//
// Every kernel in rp::dsp has the same shape: a plain state struct, an `init` that runs on the control thread, and
// a `process` that is `noexcept`, allocates nothing and may be handed any split of a stream into blocks, giving the
// same samples as one call over the whole of it (the resampler's input and output counts differ, so it takes input
// and gives output in two calls, Resample.hpp). They know nothing of a graph, a core or a file: the kit compilers
// run them in loops over whole buffers, and the rp.dsp nodes wrap the same structs.

#include <cstddef>

namespace rp::dsp {

// A linear multiply, exact: out = in * gain, sample by sample. A held gain needs no state, so a block split can
// change nothing.
struct Gain {
    float gain = 1.0f;

    void init(float linear) noexcept { gain = linear; }

    // `in` and `out` may be the same buffer.
    void process(const float* in, float* out, std::size_t frames) const noexcept;
};

// The largest absolute sample of a whole buffer, 0 for an empty one. The fold is `std::max(peak, |v|)` from 0 in
// order, which is the kit path's: a NaN sample leaves the running peak where it was.
float peak(const float* in, std::size_t frames) noexcept;

// The gain that peak-normalises a buffer whose peak is `peakLevel`, folded into a level `gain` already set:
// `gain * (1 / peakLevel)`, and `gain` alone for silence. Spelled as one function because the kit path's bytes depend
// on it being a multiply by the reciprocal rather than a division, so D2's graph normalise must compute it the same.
float normalizedGain(float gain, float peakLevel) noexcept;

} // namespace rp::dsp
