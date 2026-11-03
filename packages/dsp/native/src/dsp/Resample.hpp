#pragma once

// The resample kernel: r8brain's CDSPResampler24, fed fixed 1024-frame chunks.
//
// r8brain's output depends on how its input is split into calls, so the kernel always hands it a whole chunk: input is
// gathered into a 1024-frame chunk and resampled only once the chunk is full, and the stream past the source's end is
// silence (`pushSilence`), which zero-pads the last partial chunk and every one after it. The resampler only ever sees
// the stream cut at multiples of 1024 frames from its start, so the output stream is the same however the input
// arrives, in one call or a frame at a time. This is the kit path's resample to the bit: its bytes depend on the
// chunking.
//
// The input and output counts differ, so the kernel takes input and gives output in two calls. `push` gathers input
// and resamples each chunk it completes into an output store sized at `init` for two chunks' worst-case output;
// `pull` reads from that store. When the store cannot take another chunk's output, `push` stops consuming (it returns
// how many frames it took) until `pull` makes room, so neither ever allocates. A puller wanting N frames alternates
// the two until it has them. The resampler and the store are built in `init`, on the control thread: r8brain
// allocates there, and only there (its `process` writes into buffers it sized at construction).
//
// The run-in is part of the contract: r8brain's filter keeps output behind input, so the first chunks can produce no
// output at all (44100 -> 11468 gives its first frames after seven chunks), and the lag is in its latency, not in the
// kernel's. A consumer must therefore push ahead of what it plays, and the first frames it pulls are the source's
// first frames, not a loss.

#include <cstddef>
#include <memory>
#include <vector>

namespace rp::dsp {

class Resampler {
public:
    // The frames r8brain is handed per call; its maximum input length, fixed at construction.
    static constexpr std::size_t kChunk = 1024;

    Resampler();
    ~Resampler();
    Resampler(Resampler&&) noexcept;
    Resampler& operator=(Resampler&&) noexcept;

    // Builds the resampler and its stores for `inRate` -> `outRate` (both > 0), discarding any stream in progress.
    // Allocates, r8brain's own buffers included; throws std::bad_alloc when it cannot (a failed r8brain allocation is
    // an exception rather than the null buffer its compiled-out assertion would leave, dsp/R8bAllocator.hpp), leaving
    // the resampler unbuilt (`ready()` is false) rather than holding the old stream, so a caller that catches the throw
    // can drop it or call `init` again.
    void init(double inRate, double outRate);
    bool ready() const noexcept { return impl_ != nullptr; }

    // Gathers up to `frames` input frames, resampling each chunk it completes. Returns how many it took: fewer than
    // `frames` only when the output store is full, so a caller pulls and pushes the rest. Before `init`, takes none.
    std::size_t push(const float* in, std::size_t frames) noexcept;
    // The same with `frames` frames of silence: the stream after a source's end.
    std::size_t pushSilence(std::size_t frames) noexcept;

    // Reads up to `frames` resampled frames, in stream order; returns how many it read.
    std::size_t pull(float* out, std::size_t frames) noexcept;
    // Resampled frames waiting to be pulled.
    std::size_t available() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    template <class Fill>
    std::size_t gather(std::size_t frames, Fill fill) noexcept;
    bool flushChunk() noexcept;
};

// A whole buffer's resampled length, as the kit path cuts it: floor(frames * (outRate / inRate)), the ratio taken in
// double first.
std::size_t resampledLength(std::size_t frames, double inRate, double outRate) noexcept;

// A whole buffer resampled and cut to resampledLength: the source, then silence until the cut. Empty in, empty out.
void resample(double inRate, double outRate, const float* in, std::size_t frames, std::vector<float>& out);

} // namespace rp::dsp
