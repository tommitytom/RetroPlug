#include "dsp/Resample.hpp"

#include <algorithm>
#include <cassert>

#include "dsp/R8bAllocator.hpp"  // r8brain, with an allocation failure made an exception

namespace rp::dsp {

struct Resampler::Impl {
    Impl(double inRate, double outRate)
        : resampler(inRate, outRate, static_cast<int>(kChunk)) {
        chunk.alloc(static_cast<int>(kChunk));
        maxOut = static_cast<std::size_t>(resampler.getMaxOutLen(static_cast<int>(kChunk)));
        store.assign(2 * maxOut, 0.0f);
    }

    r8b::CDSPResampler24      resampler;
    r8b::CFixedBuffer<double> chunk;  // the chunk being gathered, as the doubles r8brain reads
    std::size_t               fill = 0;
    std::vector<float>        store;  // a ring of resampled frames
    std::size_t               maxOut = 0;  // the most one chunk can produce
    std::size_t               read = 0, count = 0;
};

Resampler::Resampler() = default;
Resampler::~Resampler() = default;
Resampler::Resampler(Resampler&&) noexcept = default;
Resampler& Resampler::operator=(Resampler&&) noexcept = default;

void Resampler::init(double inRate, double outRate) {
    impl_.reset();
    impl_ = std::make_unique<Impl>(inRate, outRate);
}

// Resamples the full chunk into the store, if the store has room for the most it can produce.
bool Resampler::flushChunk() noexcept {
    Impl& s = *impl_;
    const std::size_t capacity = s.store.size();
    if (capacity - s.count < s.maxOut) return false;

    double* produced = nullptr;
    const int n = s.resampler.process(s.chunk.getPtr(), static_cast<int>(kChunk), produced);
    // `maxOut` is getMaxOutLen's bound, and the whole ring-size rule rests on r8brain staying inside it: a chunk that
    // returned more would overwrite frames not yet pulled. (An assert rather than a clamp: release builds pay nothing.)
    assert(static_cast<std::size_t>(n) <= s.maxOut);
    std::size_t write = (s.read + s.count) % capacity;
    for (int i = 0; i < n; ++i) {
        s.store[write] = static_cast<float>(produced[i]);
        write = write + 1 == capacity ? 0 : write + 1;
    }
    s.count += static_cast<std::size_t>(n);
    s.fill = 0;
    return true;
}

template <class Fill>
std::size_t Resampler::gather(std::size_t frames, Fill fill) noexcept {
    if (!impl_) return 0;
    Impl& s = *impl_;
    std::size_t taken = 0;
    while (taken < frames) {
        if (s.fill == kChunk && !flushChunk()) break;
        const std::size_t n = std::min(frames - taken, kChunk - s.fill);
        fill(s.chunk.getPtr() + s.fill, taken, n);
        s.fill += n;
        taken += n;
    }
    // A chunk this call completed is resampled now when the store has room, so its output can be pulled at once.
    if (s.fill == kChunk) flushChunk();
    return taken;
}

std::size_t Resampler::push(const float* in, std::size_t frames) noexcept {
    return gather(frames, [in](double* dst, std::size_t from, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<double>(in[from + i]);
    });
}

std::size_t Resampler::pushSilence(std::size_t frames) noexcept {
    return gather(frames, [](double* dst, std::size_t, std::size_t n) noexcept { std::fill_n(dst, n, 0.0); });
}

std::size_t Resampler::pull(float* out, std::size_t frames) noexcept {
    if (!impl_) return 0;
    Impl& s = *impl_;
    const std::size_t capacity = s.store.size();
    const std::size_t n = std::min(frames, s.count);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = s.store[s.read];
        s.read = s.read + 1 == capacity ? 0 : s.read + 1;
    }
    s.count -= n;
    // A chunk left waiting for room is resampled as soon as there is some.
    if (s.fill == kChunk) flushChunk();
    return n;
}

std::size_t Resampler::available() const noexcept {
    return impl_ ? impl_->count : 0;
}

std::size_t resampledLength(std::size_t frames, double inRate, double outRate) noexcept {
    return static_cast<std::size_t>(static_cast<double>(frames) * (outRate / inRate));
}

void resample(double inRate, double outRate, const float* in, std::size_t frames, std::vector<float>& out) {
    if (frames == 0) {
        out.clear();
        return;
    }
    Resampler r;
    r.init(inRate, outRate);
    const std::size_t length = resampledLength(frames, inRate, outRate);
    out.resize(length);

    std::size_t consumed = 0, written = 0;
    while (written < length) {
        if (consumed < frames) consumed += r.push(in + consumed, frames - consumed);
        else r.pushSilence(Resampler::kChunk);
        written += r.pull(out.data() + written, length - written);
    }
}

} // namespace rp::dsp
