// The rp::dsp kernels (src/dsp/, docs/modular-graph-phase-d1-design.md section 4), with no graph, core or file in the
// way: what each promises a node that runs it a block at a time, and what the kit path that runs it over whole
// buffers relies on.
//
// - A stream split into blocks of 1, 7, 64 or 1024 frames (and a ragged mix) gives the samples one call over the
//   whole of it gives, bit for bit: every kernel carries its state across the split.
// - A warmed process allocates and frees nothing (the counting allocator; the C library's blocks are counted too
//   where this build can see them, which matters for r8brain, whose own allocator is malloc).
// - Biquad: the eight coefficient calculators and the loop, written out here from lsdj/Effects.cpp, give the
//   kernel's coefficients (every field bit for bit) and its filtered buffer sample for sample.
// - Dither: the same seed gives the same codes and a different seed different noise; the four algorithms without a
//   padded reference (HighPassTPDF, ShapedTPDF, ErrorDiffusion, None) give the old whole-buffer loops' codes; Sierra
//   Lite and JJN, kept as a few past errors, give the codes their padded whole-buffer arithmetic gives; and the seed
//   drawn from a buffer gives the codes test/audio/Dither.test.cpp pins through the kit effect. (inputSeed's
//   big-endian half cannot be exercised on a little-endian host; the two halves are held to one value by construction,
//   not by a case here.)
// - Resample: the output stream is independent of how the input arrives (a source shorter than one chunk included),
//   and equals a direct fixed-chunk feed of r8brain, written out here as a reference; a failed allocation in `init`
//   throws and leaves the resampler unbuilt.
//
// The kit baseline (test/audio/KitBaseline.test.cpp) holds the same kernels to whole kit banks' bytes.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

#include "CountingAllocator.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "dsp/R8bAllocator.hpp"  // r8brain, for the reference resample below
#include "dsp/Resample.hpp"

namespace {

using rp::dsp::BiquadType;
using rp::dsp::DitherAlgorithm;

constexpr std::size_t kFrames = 4096;

// The block sizes a stream is split into: the extremes, an odd size that never lands on a power of two, the usual
// host block, the resampler's chunk, and a ragged sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {1024}, {1, 31, 512, 3, 1024, 77}};

constexpr DitherAlgorithm kAlgorithms[] = {
    DitherAlgorithm::None,           DitherAlgorithm::HighPassTPDF, DitherAlgorithm::ShapedTPDF,
    DitherAlgorithm::ErrorDiffusion, DitherAlgorithm::JJN,          DitherAlgorithm::SierraLite,
};

constexpr BiquadType kBiquadTypes[] = {
    BiquadType::LowPass, BiquadType::HighPass, BiquadType::BandPass,  BiquadType::BandStop,
    BiquadType::Peak,    BiquadType::LowShelf, BiquadType::HighShelf, BiquadType::AllPass,
};

/** Uniform noise in [-amp, amp) from a fixed seed, drawn from raw mt19937 output so the sequence is specified. */
std::vector<float> noise(std::size_t frames, float amp, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<float> out(frames);
    for (auto& v : out) v = (static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * amp;
    return out;
}

/** A sine plus noise, overdriven past [-1, 1] in places, so the clamps and the error terms all do work. */
std::vector<float> signal(std::size_t frames) {
    std::vector<float> out = noise(frames, 0.3f, 0x5eed0001u);
    for (std::size_t i = 0; i < frames; ++i) out[i] += 1.1f * std::sin(static_cast<float>(i) * 0.031f);
    return out;
}

/** Calls `run(offset, n)` over [0, frames) in the block sizes `split` cycles through. */
template <class Run>
void inBlocks(std::size_t frames, const std::vector<std::size_t>& split, Run run) {
    std::size_t at = 0, k = 0;
    while (at < frames) {
        const std::size_t n = std::min(split[k++ % split.size()], frames - at);
        run(at, n);
        at += n;
    }
}

/** The first index two buffers differ at, bit for bit, or -1. One number keeps a failing CHECK readable. */
template <class T>
std::ptrdiff_t firstDifference(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(T)) != 0) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

/** The same bits, so a -0.0f where 0.0f was expected counts as a difference where `==` would not. */
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// ---- the padded whole-buffer forms of Sierra Lite and JJN (the legacy AudioDithering arithmetic) ----------------
// Codes as floats in [0, 2^bits - 1], so the kernel's streaming form is held to the padded arithmetic bit for bit.

std::vector<float> paddedSierraLite(std::vector<float> buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    const std::size_t n = buf.size();
    std::vector<float> padded(n + 2, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        padded[i] = (buf[i] + 1.0f) * halfScale;

    for (std::size_t i = 0; i < n; ++i) {
        const float value = padded[i];
        const float q = std::round(std::clamp(value, 0.0f, maxValue));
        buf[i] = q;
        const float err = value - q;
        if (i + 1 < n) padded[i + 1] += err * 0.5f;
        if (i + 2 < n) padded[i + 2] += err * 0.25f;
    }
    return buf;
}

std::vector<float> paddedJjn(std::vector<float> buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    const std::size_t n = buf.size();
    std::vector<float> padded(n + 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        padded[i] = (buf[i] + 1.0f) * halfScale;

    constexpr float w1 = 7.0f / 48.0f;
    constexpr float w2 = 5.0f / 48.0f;
    constexpr float w3 = 3.0f / 48.0f;
    constexpr float w4 = 1.0f / 48.0f;

    for (std::size_t i = 0; i < n; ++i) {
        const float value = padded[i];
        const float q = std::round(std::clamp(value, 0.0f, maxValue));
        buf[i] = q;
        const float err = value - q;
        if (i + 1 < n) padded[i + 1] += err * w1;
        if (i + 2 < n) padded[i + 2] += err * w2;
        if (i + 3 < n) padded[i + 3] += err * w3;
        if (i + 4 < n) padded[i + 4] += err * w4;
    }
    return buf;
}

// ---- the kit path's dithers as they were before the kernel ---------------------------------------------------------
// Verbatim from 12905a59's lsdj/Effects.cpp (the two TPDF algorithms and error diffusion) and KitUtil.cpp's no-dither
// branch. The RNG is handed in rather than drawn from the buffer here: the kernel salts `inputSeed` itself, so the
// reference starts from `mt19937(inputSeed ^ salt)`, the word the kernel's `init` seeds. Codes as floats in [0, 2^bits
// - 1], the same shape the padded references give.

constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fold(fnv1a64("HighPassTPDF"))
constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fold(fnv1a64("ShapedTPDF"))

float uniform01(std::mt19937& rng) { return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f); }

std::mt19937 ditherRng(const std::vector<float>& buf, std::uint32_t salt) {
    return std::mt19937(rp::dsp::inputSeed(buf.data(), buf.size()) ^ salt);
}

std::vector<float> highPassTPDF(std::vector<float>& buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    constexpr float quantum = 1.0f;

    std::mt19937 rng = ditherRng(buf, kHighPassTPDFSalt);

    float prevDither   = 0.0f;
    float shapingState = 0.0f;

    for (float& v : buf) {
        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
        float shaped = dither - prevDither * 0.94f;
        prevDither = dither;
        shaped -= shapingState * 0.5f;
        float value = (v + 1.0f) * halfScale + shaped;
        value = std::clamp(value, 0.0f, maxValue);
        const float q = std::round(value);
        shapingState = q - value;
        v = q;
    }
    return buf;
}

std::vector<float> shapedTPDF2ndOrder(std::vector<float>& buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    constexpr float quantum = 1.0f;

    std::mt19937 rng = ditherRng(buf, kShapedTPDFSalt);

    float err1 = 0.0f, err2 = 0.0f;
    for (float& v : buf) {
        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
        // Coefficients tuned for ~11 kHz target rate (LSDJ kit sample rate)
        float value = (v + 1.0f) * halfScale + dither + (1.623f * err1) - (0.982f * err2);
        value = std::clamp(value, 0.0f, maxValue);
        const float q = std::round(value);
        const float qe = q - value;
        err2 = err1;
        err1 = qe;
        v = q;
    }
    return buf;
}

std::vector<float> errorDiffusion(std::vector<float>& buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    float error = 0.0f;
    for (float& v : buf) {
        float value = (v + 1.0f) * halfScale;
        value += error * 0.9f;
        value = std::clamp(value, 0.0f, maxValue);
        const float q = std::round(value);
        error = value - q;
        v = q;
    }
    return buf;
}

/** The no-dither branch of the old `KitUtil::compileSample` (clamp, scale to [0, 1], round to the code), with the kit
 *  path's hard-coded 15.0f written as the maximum any depth gives. */
std::vector<float> noDither(std::vector<float>& buf, int bitDepth) {
    const float maxValue = static_cast<float>((1 << bitDepth) - 1);
    for (float& v : buf) {
        v = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f;
        v = std::round(v * maxValue);
    }
    return buf;
}

// ---- the kit path's biquad as it was before the kernel ------------------------------------------------------------
// The eight RBJ calculators and the loop, copied verbatim from lsdj/Effects.cpp with their types, so the bodies need no
// edit and this file needs no reflect-cpp or core link: what the kernel's coefficients and its filtered buffer are
// held to, bit for bit.

constexpr float kPi = 3.14159265358979323846f;

enum class FilterType : std::uint32_t {
    LowPass   = 0,
    HighPass  = 1,
    BandPass  = 2,
    BandStop  = 3,
    Peak      = 4,
    LowShelf  = 5,
    HighShelf = 6,
    AllPass   = 7,
};

struct FilterEffect {
    FilterType filterType = FilterType::LowPass;
    float      frequency  = 5734.0f;   // GAMEBOY_SAMPLE_RATE / 2; nyquist for 11468 Hz
    float      q          = 1.0f;
    float      gain       = 0.0f;      // dB, used by Peak / LowShelf / HighShelf
};

struct FilterState {
    float sampleRate = 48000.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

void calculateLowPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - cw) / (2.0f * a0);
    s.b1 = (1.0f - cw) / a0;
    s.b2 = (1.0f - cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateHighPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  (1.0f + cw) / (2.0f * a0);
    s.b1 = -(1.0f + cw) / a0;
    s.b2 =  (1.0f + cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateBandPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  sw / (2.0f * a0);
    s.b1 = 0.0f;
    s.b2 = -sw / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateBandStop(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = 1.0f / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = 1.0f / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculatePeak(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha / A;
    s.b0 = (1.0f + alpha * A) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f - alpha * A) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha / A) / a0;
}

void calculateLowShelf(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float beta = std::sqrt(A) / effect.q;
    const float a0 = (A + 1.0f) + (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) + (A - 1.0f) * cw - beta * sw) / a0;
}

void calculateHighShelf(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float beta = std::sqrt(A) / effect.q;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) - (A - 1.0f) * cw - beta * sw) / a0;
}

void calculateAllPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - alpha) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f + alpha) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void updateCoefficients(const FilterEffect& effect, FilterState& s) {
    switch (effect.filterType) {
        case FilterType::LowPass:   calculateLowPass(effect, s);   break;
        case FilterType::HighPass:  calculateHighPass(effect, s);  break;
        case FilterType::BandPass:  calculateBandPass(effect, s);  break;
        case FilterType::BandStop:  calculateBandStop(effect, s);  break;
        case FilterType::Peak:      calculatePeak(effect, s);      break;
        case FilterType::LowShelf:  calculateLowShelf(effect, s);  break;
        case FilterType::HighShelf: calculateHighShelf(effect, s); break;
        case FilterType::AllPass:   calculateAllPass(effect, s);   break;
    }
}

/** The old whole-buffer pass: the coefficients once, then the direct form I loop, its feedback before the clamp. */
std::vector<float> referenceBiquad(const FilterEffect& effect, float sampleRate, std::vector<float> target) {
    FilterState s;
    s.sampleRate = sampleRate;
    updateCoefficients(effect, s);

    for (float& sample : target) {
        const float in = sample;
        float out = s.b0 * in + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
        s.x2 = s.x1; s.x1 = in;
        s.y2 = s.y1; s.y1 = out;
        out = std::clamp(out, -1.0f, 1.0f);
        sample = out;
    }
    return target;
}

/** The coefficients the old calculators give, for the field-for-field comparison. */
FilterState referenceFilterState(const FilterEffect& effect, float sampleRate) {
    FilterState s;
    s.sampleRate = sampleRate;
    updateCoefficients(effect, s);
    return s;
}

// ---- the kit path's resample, fed straight to r8brain -----------------------------------------------------------
// One resampler per call, a fixed 1024-frame chunk zero-padded at the tail, the output cut to floor(n * out / in).

std::vector<float> referenceResample(double inRate, double outRate, const std::vector<float>& buffer) {
    std::vector<float> target;
    if (buffer.empty()) return target;
    constexpr std::size_t kInBufCapacity = 1024;
    r8b::CFixedBuffer<double> inBuf;
    inBuf.alloc(static_cast<int>(kInBufCapacity));
    r8b::CPtrKeeper<r8b::CDSPResampler24*> resampler =
        new r8b::CDSPResampler24(inRate, outRate, static_cast<int>(kInBufCapacity));
    const std::size_t targetSize = static_cast<std::size_t>(buffer.size() * (outRate / inRate));
    target.resize(targetSize);
    std::size_t sourcePos = 0, targetPos = 0;
    while (targetPos < targetSize) {
        std::fill_n(inBuf.getPtr(), kInBufCapacity, 0.0);
        const std::size_t chunkSize = std::min(kInBufCapacity, buffer.size() - sourcePos);
        for (std::size_t i = 0; i < chunkSize; ++i) inBuf[i] = static_cast<double>(buffer[sourcePos++]);
        double* out = nullptr;
        std::size_t writeCount =
            static_cast<std::size_t>(resampler->process(inBuf.getPtr(), static_cast<int>(kInBufCapacity), out));
        if (targetPos + writeCount > targetSize) writeCount = targetSize - targetPos;
        for (std::size_t i = 0; i < writeCount; ++i) target[targetPos++] = static_cast<float>(out[i]);
    }
    return target;
}

/**
 * The resampler driven as a block-streaming consumer would drive it: input pushed in `pushSplit` pieces, output
 * pulled in `pullSplit` pieces, silence after the source, until `length` frames are out.
 */
std::vector<float> streamResample(double inRate, double outRate, const std::vector<float>& in, std::size_t length,
                                  const std::vector<std::size_t>& pushSplit,
                                  const std::vector<std::size_t>& pullSplit) {
    rp::dsp::Resampler r;
    r.init(inRate, outRate);
    std::vector<float> out(length);
    std::size_t consumed = 0, written = 0, k = 0, j = 0;
    while (written < length) {
        const std::size_t want = pushSplit[k++ % pushSplit.size()];
        if (consumed < in.size()) consumed += r.push(in.data() + consumed, std::min(want, in.size() - consumed));
        else r.pushSilence(want);
        const std::size_t pull = std::min(pullSplit[j++ % pullSplit.size()], length - written);
        written += r.pull(out.data() + written, pull);
    }
    return out;
}

} // namespace

TEST_CASE("the gain kernel multiplies exactly and a block split changes nothing", "[dsp][gain]") {
    const std::vector<float> in = signal(kFrames);
    rp::dsp::Gain g;
    g.init(0.37f);
    std::vector<float> whole(kFrames);
    g.process(in.data(), whole.data(), kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) REQUIRE(whole[i] == in[i] * 0.37f);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        std::vector<float> blocks(kFrames);
        inBlocks(kFrames, split,
                 [&](std::size_t at, std::size_t n) { g.process(in.data() + at, blocks.data() + at, n); });
        CHECK(firstDifference(whole, blocks) == -1);
    }

    // In place, as the kit path runs it.
    std::vector<float> inPlace = in;
    g.process(inPlace.data(), inPlace.data(), kFrames);
    CHECK(firstDifference(whole, inPlace) == -1);
}

TEST_CASE("peak and the normalising gain are the kit path's", "[dsp][gain]") {
    const std::vector<float> in = {0.25f, -0.8f, 0.5f, std::nanf(""), -0.1f};
    CHECK(rp::dsp::peak(in.data(), in.size()) == 0.8f);  // a NaN leaves the running peak where it was
    CHECK(rp::dsp::peak(in.data(), 0) == 0.0f);
    // 0.37 * (1/0.9) and 0.37 / 0.9 differ in binary32 (0.411111146 against 0.411111116), and neither operand is a
    // power of two, so this pair can tell the reciprocal multiply the kit path's bytes depend on from a division. A
    // round pair cannot: 1/0.8 rounds to exactly 1.25f, so 0.5 * (1/0.8) and 0.5 / 0.8 are one float.
    CHECK(rp::dsp::normalizedGain(0.37f, 0.9f) == 0.37f * (1.0f / 0.9f));
    CHECK(rp::dsp::normalizedGain(0.37f, 0.9f) != 0.37f / 0.9f);
    CHECK(rp::dsp::normalizedGain(0.5f, 0.0f) == 0.5f);  // silence keeps the gain as set
}

TEST_CASE("the biquad kernel is the kit path's filter to the bit", "[dsp][biquad]") {
    // The calculators and the loop above are lsdj/Effects.cpp's, so this holds the kernel to the arithmetic the kit
    // baseline's bytes were recorded from: every coefficient bit for bit (not ==, which cannot tell -0.0f from 0.0f)
    // and the filtered buffer sample for sample, over the same eight types and signal the streaming case uses. The
    // shape is swept as well as the type: a reciprocal substituted for a division can round the same at one
    // frequency and differently at another (44100/1500/0.9 does not see it, 22050/500/0.9 does), so one shape would
    // leave that edit to the kit baseline, which a 1-ulp coefficient moves only if a code happens to flip.
    struct Shape {
        float rate, frequency, q, gainDb;
    };
    const Shape shapes[] = {
        {44100.0f, 1500.0f, 0.9f, 5.0f},  // the streaming case's
        {22050.0f, 500.0f, 0.9f, 5.0f},   // low and slow, where a coefficient's last bit shows
        {11468.0f, 9000.0f, 2.0f, 5.0f},  // at the kit rate, toward nyquist, with a high Q
        {48000.0f, 5000.0f, 0.5f, -6.0f}, // a cut, so the shelves' gain term is negative
    };
    const std::vector<float> in = signal(kFrames);
    for (const BiquadType type : kBiquadTypes) {
        for (const Shape& shape : shapes) {
            INFO("type " << static_cast<int>(type) << ", " << shape.rate << " Hz, " << shape.frequency << " Hz, q "
                         << shape.q);
            const rp::dsp::BiquadSettings settings{type, shape.rate, shape.frequency, shape.q, shape.gainDb};
            const FilterEffect old{static_cast<FilterType>(static_cast<std::uint32_t>(type)), shape.frequency,
                                   shape.q, shape.gainDb};

            const rp::dsp::BiquadCoefficients c = rp::dsp::biquadCoefficients(settings);
            const FilterState s = referenceFilterState(old, settings.sampleRate);
            CHECK(sameBits(c.b0, s.b0));
            CHECK(sameBits(c.b1, s.b1));
            CHECK(sameBits(c.b2, s.b2));
            CHECK(sameBits(c.a1, s.a1));
            CHECK(sameBits(c.a2, s.a2));

            rp::dsp::Biquad b;
            b.init(settings);
            std::vector<float> filtered(kFrames);
            b.process(in.data(), filtered.data(), kFrames);
            CHECK(firstDifference(referenceBiquad(old, settings.sampleRate, in), filtered) == -1);
        }
    }
}

TEST_CASE("the biquad kernel streams: any block split gives one call's output", "[dsp][biquad]") {
    const std::vector<float> in = signal(kFrames);
    for (const BiquadType type : kBiquadTypes) {
        INFO("type " << static_cast<int>(type));
        const rp::dsp::BiquadSettings settings{type, 44100.0f, 1500.0f, 0.9f, 5.0f};
        rp::dsp::Biquad b;
        b.init(settings);
        std::vector<float> whole(kFrames);
        b.process(in.data(), whole.data(), kFrames);
        bool clamped = false;
        for (float v : whole) {
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            clamped = clamped || v == 1.0f || v == -1.0f;
        }
        // The overdriven sine (about 220 Hz) reaches the clamp through every filter that passes it.
        if (type != BiquadType::HighPass && type != BiquadType::BandPass) CHECK(clamped);

        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            b.init(settings);
            std::vector<float> blocks(kFrames);
            inBlocks(kFrames, split,
                     [&](std::size_t at, std::size_t n) { b.process(in.data() + at, blocks.data() + at, n); });
            CHECK(firstDifference(whole, blocks) == -1);
        }
    }
}

TEST_CASE("setup keeps a biquad's state while init clears it", "[dsp][biquad]") {
    const std::vector<float> in = signal(256);
    const rp::dsp::BiquadSettings a{BiquadType::LowPass, 48000.0f, 800.0f, 0.7f, 0.0f};
    const rp::dsp::BiquadSettings b{BiquadType::LowPass, 48000.0f, 3000.0f, 0.7f, 0.0f};
    rp::dsp::Biquad swept;
    swept.init(a);
    std::vector<float> out(256);
    swept.process(in.data(), out.data(), 128);
    const float y1 = swept.y1;
    swept.setup(b);
    CHECK(swept.y1 == y1);
    swept.init(b);
    CHECK(swept.y1 == 0.0f);
}

TEST_CASE("the dither kernel streams: any block split gives one call's codes", "[dsp][dither]") {
    const std::vector<float> in = signal(kFrames);
    for (const DitherAlgorithm algo : kAlgorithms) {
        for (const int bits : {1, 4, 8, 16}) {
            INFO("algorithm " << static_cast<int>(algo) << ", " << bits << " bits");
            rp::dsp::Dither d;
            d.init(algo, bits, 0x1234abcdu);
            std::vector<std::uint16_t> whole(kFrames);
            d.process(in.data(), whole.data(), kFrames);
            const std::uint32_t maxCode = (1u << bits) - 1u;
            for (auto c : whole) REQUIRE(c <= maxCode);

            for (const auto& split : kSplits) {
                INFO("split starting " << split[0]);
                d.init(algo, bits, 0x1234abcdu);
                std::vector<std::uint16_t> blocks(kFrames);
                inBlocks(kFrames, split,
                         [&](std::size_t at, std::size_t n) { d.process(in.data() + at, blocks.data() + at, n); });
                CHECK(firstDifference(whole, blocks) == -1);
            }

            // The audio form is the same codes mapped through codeToLevel, block split or not, in place or not.
            d.init(algo, bits, 0x1234abcdu);
            std::vector<float> levels = in;
            inBlocks(kFrames, kSplits.back(), [&](std::size_t at, std::size_t n) {
                d.processLevels(levels.data() + at, levels.data() + at, n);
            });
            std::vector<float> mapped(kFrames);
            for (std::size_t i = 0; i < kFrames; ++i) mapped[i] = rp::dsp::codeToLevel(whole[i], bits);
            CHECK(firstDifference(mapped, levels) == -1);
        }
    }
}

TEST_CASE("codes map back to the quantised levels", "[dsp][dither]") {
    CHECK(rp::dsp::codeToLevel(0, 4) == -1.0f);
    CHECK(rp::dsp::codeToLevel(15, 4) == 1.0f);
    CHECK(rp::dsp::codeToLevel(1, 1) == 1.0f);
    CHECK(rp::dsp::codeToLevel(65535, 16) == 1.0f);
    // The LSDj kit decoder's level for a 4-bit code (SampleUtil::convertNibblesToF32), so a dither node's audio is
    // what the Game Boy would play.
    for (std::uint32_t c = 0; c < 16; ++c) {
        CHECK(rp::dsp::codeToLevel(c, 4) == (static_cast<float>(c) / 15.0f) * 2.0f - 1.0f);
    }
}

TEST_CASE("init clamps the bit depth and a NaN reads as the lowest code", "[dsp][dither]") {
    // Both are documented behaviour a node's setting and a garbage sample depend on: the clamp in Dither.hpp, the NaN
    // rule in Dither.cpp's toCode (an undefined float-to-int cast otherwise).
    rp::dsp::Dither d;
    d.init(DitherAlgorithm::None, 0, 0);
    CHECK(d.bits == rp::dsp::kMinDitherBits);
    CHECK(d.maxValue == 1.0f);
    d.init(DitherAlgorithm::None, 24, 0);
    CHECK(d.bits == rp::dsp::kMaxDitherBits);
    CHECK(d.maxValue == 65535.0f);
    CHECK(rp::dsp::codeToLevel(3, 0) == rp::dsp::codeToLevel(3, rp::dsp::kMinDitherBits));
    CHECK(rp::dsp::codeToLevel(3, 24) == rp::dsp::codeToLevel(3, rp::dsp::kMaxDitherBits));

    const std::vector<float> nanos = {std::nanf(""), 0.5f, -std::nanf("")};
    std::vector<std::uint16_t> codes(nanos.size());
    d.init(DitherAlgorithm::None, 4, 0);
    d.process(nanos.data(), codes.data(), codes.size());
    CHECK(codes[0] == 0);
    CHECK(codes[1] == 11);  // round((0.5 + 1) * 0.5 * 15)
    CHECK(codes[2] == 0);
}

TEST_CASE("the same seed gives the same codes, a different seed different noise", "[dsp][dither]") {
    // Silence is the cleanest probe: every code a TPDF dither gives it comes from the noise.
    const std::vector<float> silence(1024, 0.0f);
    for (const DitherAlgorithm algo : {DitherAlgorithm::HighPassTPDF, DitherAlgorithm::ShapedTPDF}) {
        INFO("algorithm " << static_cast<int>(algo));
        const auto run = [&](std::uint32_t seed) {
            rp::dsp::Dither d;
            d.init(algo, 4, seed);
            std::vector<std::uint16_t> codes(silence.size());
            d.process(silence.data(), codes.data(), silence.size());
            return codes;
        };
        CHECK(firstDifference(run(42), run(42)) == -1);
        const auto a = run(42), b = run(43);
        std::size_t differ = 0;
        for (std::size_t i = 0; i < a.size(); ++i) differ += a[i] != b[i];
        CHECK(differ > 16);
    }

    // The two TPDF algorithms salt one seed differently, so they do not walk one noise sequence.
    rp::dsp::Dither hp, sh;
    hp.init(DitherAlgorithm::HighPassTPDF, 4, 7);
    sh.init(DitherAlgorithm::ShapedTPDF, 4, 7);
    std::uint32_t hpFirst = hp.rng(), shFirst = sh.rng();
    CHECK(hpFirst != shFirst);

    // The deterministic algorithms ignore the seed.
    for (const DitherAlgorithm algo : {DitherAlgorithm::None, DitherAlgorithm::ErrorDiffusion, DitherAlgorithm::JJN,
                                       DitherAlgorithm::SierraLite}) {
        INFO("algorithm " << static_cast<int>(algo));
        const std::vector<float> in = signal(512);
        rp::dsp::Dither x, y;
        x.init(algo, 4, 1);
        y.init(algo, 4, 2);
        std::vector<std::uint16_t> cx(512), cy(512);
        x.process(in.data(), cx.data(), 512);
        y.process(in.data(), cy.data(), 512);
        CHECK(firstDifference(cx, cy) == -1);
    }
}

TEST_CASE("the input seed gives the codes the kit path pinned", "[dsp][dither]") {
    // test/audio/Dither.test.cpp pins these eight codes for a silent eight-sample buffer through the kit effect; the
    // kernel seeded from inputSeed over the same buffer must give them, which holds the hash, the fold, the salts, the
    // draw and both algorithms at once.
    const std::vector<float> silence(8, 0.0f);
    const std::uint32_t seed = rp::dsp::inputSeed(silence.data(), silence.size());
    const auto codes = [&](DitherAlgorithm algo) {
        rp::dsp::Dither d;
        d.init(algo, 4, seed);
        std::vector<std::uint16_t> out(8);
        d.process(silence.data(), out.data(), 8);
        return out;
    };
    CHECK(codes(DitherAlgorithm::HighPassTPDF) == std::vector<std::uint16_t>{7, 8, 7, 7, 8, 7, 8, 8});
    CHECK(codes(DitherAlgorithm::ShapedTPDF) == std::vector<std::uint16_t>{7, 7, 7, 7, 8, 7, 8, 7});
    // A function of the samples: one changed bit moves it.
    std::vector<float> nudged = silence;
    nudged[5] = 1.0e-30f;
    CHECK(rp::dsp::inputSeed(nudged.data(), nudged.size()) != seed);
}

TEST_CASE("Sierra Lite and JJN as past errors give their padded arithmetic's codes", "[dsp][dither]") {
    // Lengths below and around the error window (0 to 5) exercise the terms a padded copy never had before the
    // stream's start and the ones it dropped past its end; the long ones, overdriven noise and a quiet signal, put
    // every error term to work, and a split into blocks puts the carried errors across every block edge.
    std::vector<std::vector<float>> inputs;
    for (std::size_t n = 0; n <= 5; ++n) inputs.push_back(noise(n, 1.3f, 0x51e77a00u + static_cast<std::uint32_t>(n)));
    inputs.push_back(noise(kFrames, 1.3f, 0x51e77a10u));
    inputs.push_back(noise(kFrames, 0.02f, 0x51e77a11u));
    inputs.push_back(signal(kFrames));

    for (const int bits : {1, 4, 8, 16}) {
        for (const auto& in : inputs) {
            INFO(bits << " bits, " << in.size() << " frames");
            for (const DitherAlgorithm algo : {DitherAlgorithm::SierraLite, DitherAlgorithm::JJN}) {
                INFO("algorithm " << static_cast<int>(algo));
                const std::vector<float> reference =
                    algo == DitherAlgorithm::SierraLite ? paddedSierraLite(in, bits) : paddedJjn(in, bits);
                for (const auto& split : kSplits) {
                    INFO("split starting " << split[0]);
                    rp::dsp::Dither d;
                    d.init(algo, bits, 0);
                    std::vector<std::uint16_t> codes(in.size());
                    inBlocks(in.size(), split, [&](std::size_t at, std::size_t n) {
                        d.process(in.data() + at, codes.data() + at, n);
                    });
                    std::vector<float> asFloats(codes.begin(), codes.end());
                    CHECK(firstDifference(reference, asFloats) == -1);
                }
            }
        }
    }
}

TEST_CASE("the dithers without a padded reference give the old whole-buffer loops' codes", "[dsp][dither]") {
    // HighPassTPDF (the kit default), ShapedTPDF, ErrorDiffusion and None were held only by the streaming case, which
    // compares the kernel with itself, and by the eight-code golden, which cannot see a reordering: on silence
    // (in + 1) * halfScale and in * halfScale + halfScale are both exactly 7.5. The references below are the legacy
    // loops verbatim, seeded from the word the kernel salts, over the padded pair's inputs and splits at three depths:
    // the kit's 4 bits, 8, and 16, where a rounding of `value` one ulp apart reaches a code's boundary often enough to
    // show. At 4 bits the gap is wide: one ulp of a value near 8 is about a millionth of a code, so a flip is roughly a
    // one-in-a-million sample (an estimate, not a measurement), which is why the kit baseline cannot see it.
    std::vector<std::vector<float>> inputs;
    for (std::size_t n = 0; n <= 5; ++n) inputs.push_back(noise(n, 1.3f, 0x51e77b00u + static_cast<std::uint32_t>(n)));
    inputs.push_back(noise(kFrames, 1.3f, 0x51e77b10u));
    inputs.push_back(noise(kFrames, 0.02f, 0x51e77b11u));
    inputs.push_back(signal(kFrames));

    for (const int bits : {4, 8, 16}) {
        for (const auto& in : inputs) {
            INFO(bits << " bits, " << in.size() << " frames");
            for (const DitherAlgorithm algo : {DitherAlgorithm::None, DitherAlgorithm::HighPassTPDF,
                                              DitherAlgorithm::ShapedTPDF, DitherAlgorithm::ErrorDiffusion}) {
                INFO("algorithm " << static_cast<int>(algo));
                std::vector<float> reference = in;
                switch (algo) {
                    case DitherAlgorithm::None:         reference = noDither(reference, bits); break;
                    case DitherAlgorithm::HighPassTPDF: reference = highPassTPDF(reference, bits); break;
                    case DitherAlgorithm::ShapedTPDF:   reference = shapedTPDF2ndOrder(reference, bits); break;
                    default:                            reference = errorDiffusion(reference, bits); break;
                }
                const std::uint32_t seed = rp::dsp::inputSeed(in.data(), in.size());
                for (const auto& split : kSplits) {
                    INFO("split starting " << split[0]);
                    rp::dsp::Dither d;
                    d.init(algo, bits, seed);
                    std::vector<std::uint16_t> codes(in.size());
                    inBlocks(in.size(), split, [&](std::size_t at, std::size_t n) {
                        d.process(in.data() + at, codes.data() + at, n);
                    });
                    std::vector<float> asFloats(codes.begin(), codes.end());
                    CHECK(firstDifference(reference, asFloats) == -1);
                }
            }
        }
    }
}

TEST_CASE("the resampler's output stream is independent of how its input arrives", "[dsp][resample]") {
    struct Case {
        double inRate, outRate;
        std::size_t frames;
    };
    // The kit's rates: LSDj's 11468 Hz from the three source rates, and risa DMC rates up and down. 600 frames is a
    // source shorter than one chunk; 1024 exactly one; 5000 a few chunks and a partial; 20000 enough output that the
    // rare sample a different chunking would move is likely to be in it. (r8brain's DOUBLE output depends on how its
    // input is split into calls in most samples; the cast to float hides nearly all of that, so only a long stream
    // shows the difference reliably. A kernel that resampled a partial chunk at a push's end fails here at 44100 ->
    // 16965.4 while every kit-baseline bank still matched.)
    const Case cases[] = {
        {44100.0, 11468.0, 5000},   {22050.0, 11468.0, 5000},   {48000.0, 11468.0, 600},
        {44100.0, 11468.0, 1024},   {44100.0, 16965.40, 5000},  {22050.0, 33252.09, 600},
        {22050.0, 33252.09, 20000}, {48000.0, 4177.40, 20000},
    };
    for (const Case& c : cases) {
        INFO(c.inRate << " -> " << c.outRate << ", " << c.frames << " frames");
        const std::vector<float> in = signal(c.frames);
        const std::vector<float> reference = referenceResample(c.inRate, c.outRate, in);
        REQUIRE(reference.size() == rp::dsp::resampledLength(c.frames, c.inRate, c.outRate));
        REQUIRE(!reference.empty());

        std::vector<float> whole;
        rp::dsp::resample(c.inRate, c.outRate, in.data(), in.size(), whole);
        CHECK(firstDifference(reference, whole) == -1);

        for (const auto& pushSplit : kSplits) {
            for (const auto& pullSplit : std::initializer_list<std::vector<std::size_t>>{{1}, {64}, {4096}, {5, 300}}) {
                INFO("push split starting " << pushSplit[0] << ", pull split starting " << pullSplit[0]);
                const auto streamed = streamResample(c.inRate, c.outRate, in, reference.size(), pushSplit, pullSplit);
                CHECK(firstDifference(reference, streamed) == -1);
            }
        }
    }

    std::vector<float> none{1.0f};
    rp::dsp::resample(44100.0, 11468.0, nullptr, 0, none);
    CHECK(none.empty());
}

TEST_CASE("resamplers built and run on parallel threads each give the single-thread stream", "[dsp][resample]") {
    // The kit compiler resamples every slot on its own worker at once, and r8brain shares one filter cache between
    // all its resamplers (built under its own lock). Each thread here builds its own kernel from scratch, at rates
    // that share cached filters, and must give what one thread alone gives; under ThreadSanitizer this is the case
    // that shows the kernels share nothing unguarded.
    const std::vector<float> in = signal(6000);
    constexpr double kRates[] = {44100.0, 22050.0, 48000.0, 44100.0, 22050.0, 48000.0, 44100.0, 22050.0};
    std::vector<std::vector<float>> alone(std::size(kRates)), together(std::size(kRates));
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        rp::dsp::resample(kRates[t], 11468.0, in.data(), in.size(), alone[t]);
    }

    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        threads.emplace_back([&, t] { rp::dsp::resample(kRates[t], 11468.0, in.data(), in.size(), together[t]); });
    }
    for (auto& th : threads) th.join();
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        INFO("thread " << t << " at " << kRates[t]);
        CHECK(firstDifference(alone[t], together[t]) == -1);
    }
}

TEST_CASE("the resampler's available count follows what is pushed and pulled", "[dsp][resample]") {
    // The store/ring contract by name: a chunk pushed in shows as output to pull, and a pull takes it back down
    // (r8brain's latency means "a chunk" is not necessarily the first one to produce anything).
    rp::dsp::Resampler r;
    r.init(44100.0, 11468.0);
    const std::vector<float> in(rp::dsp::Resampler::kChunk, 0.25f);
    CHECK(r.available() == 0);
    std::size_t pushed = 0;
    while (r.available() == 0 && pushed < 8 * rp::dsp::Resampler::kChunk) pushed += r.push(in.data(), in.size());
    const std::size_t produced = r.available();
    CHECK(produced > 0);
    // The loop's own bound proves nothing, so both ends do: the run-in keeps output from the first chunk, and the
    // output arrived before the bound, so the loop stopped on `available`, not on the bound.
    CHECK(pushed > rp::dsp::Resampler::kChunk);
    CHECK(pushed < 8 * rp::dsp::Resampler::kChunk);

    float out[64] = {};
    CHECK(r.pull(out, std::size(out)) == std::size(out));
    CHECK(r.available() == produced - std::size(out));
}

TEST_CASE("a failed r8brain allocation throws out of init and leaves it unbuilt", "[dsp][resample]") {
    // r8brain allocates its filter banks and buffers in the constructor and checks each result with an assertion that
    // compiles to nothing, so dsp/R8bAllocator.hpp points its two memory hooks at an allocator that throws. Pin that
    // the hooks reach the library, then drive `init`'s failure itself: the limit is the allocator's test seam, and one
    // byte makes the very first allocation fail, so nothing is left half-built to leak.
    static_assert(std::is_base_of_v<rp::dsp::R8bAllocator, r8b::CDSPResampler24>,
                  "r8brain's resampler must be built on the throwing allocator");
    static_assert(std::is_base_of_v<rp::dsp::R8bAllocator, r8b::CFixedBuffer<double>>,
                  "r8brain's buffers must come from the throwing allocator");

    struct Limit {
        explicit Limit(std::size_t bytes) { rp::dsp::R8bAllocator::allocationLimitForTest = bytes; }
        ~Limit() { rp::dsp::R8bAllocator::allocationLimitForTest = 0; }
        Limit(const Limit&)            = delete;
        Limit& operator=(const Limit&) = delete;
    };

    rp::dsp::Resampler r;
    r.init(44100.0, 11468.0);
    REQUIRE(r.ready());
    {
        const Limit failing(1);
        // Both hooks the library reaches a buffer through throw rather than hand back a null.
        CHECK_THROWS_AS(rp::dsp::R8bAllocator::allocmem(64), std::bad_alloc);
        CHECK_THROWS_AS(rp::dsp::R8bAllocator::reallocmem(nullptr, 64), std::bad_alloc);
        CHECK_THROWS_AS(r.init(44100.0, 11468.0), std::bad_alloc);
        CHECK_FALSE(r.ready());
    }
    // Unbuilt, not broken: the same resampler builds again once the memory is there.
    r.init(44100.0, 11468.0);
    CHECK(r.ready());
}

TEST_CASE("a re-initialised resampler starts a fresh stream", "[dsp][resample]") {
    const std::vector<float> in = signal(3000);
    std::vector<float> fresh;
    rp::dsp::resample(44100.0, 11468.0, in.data(), in.size(), fresh);

    rp::dsp::Resampler r;
    r.init(44100.0, 11468.0);
    float discard[256] = {};
    r.push(in.data(), in.size());
    (void)r.pull(discard, std::size(discard));  // part of the old stream read, the rest dropped by the re-init
    r.init(44100.0, 11468.0);
    CHECK(r.available() == 0);

    std::vector<float> again(fresh.size());
    std::size_t consumed = 0, written = 0;
    while (written < again.size()) {
        if (consumed < in.size()) {
            consumed += r.push(in.data() + consumed, std::min(rp::dsp::Resampler::kChunk, in.size() - consumed));
        } else {
            r.pushSilence(rp::dsp::Resampler::kChunk);
        }
        written += r.pull(again.data() + written, again.size() - written);
    }
    CHECK(firstDifference(fresh, again) == -1);
}

TEST_CASE("a resampler that is not built takes nothing and gives nothing", "[dsp][resample]") {
    rp::dsp::Resampler r;
    const float x[4] = {};
    float y[4] = {};
    CHECK(!r.ready());
    CHECK(r.push(x, 4) == 0);
    CHECK(r.pushSilence(4) == 0);
    CHECK(r.pull(y, 4) == 0);
    CHECK(r.available() == 0);
}

TEST_CASE("an initialised kernel processes without allocating or freeing", "[dsp][noalloc]") {
    // The C library's blocks are counted too (r8brain allocates through malloc), where this build can see them: a
    // plain glibc build. Under ASan/TSan, on Windows and on macOS the operator-new half is what the zeros below hold
    // (see CountingAllocator.hpp).
    const std::vector<float> in = signal(kFrames);
    std::vector<float> out(kFrames);
    std::vector<std::uint16_t> codes(kFrames);

    rp::dsp::Gain gain;
    gain.init(0.5f);
    rp::dsp::Biquad biquad;
    biquad.init({BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f});
    rp::dsp::Dither dithers[std::size(kAlgorithms)];
    for (std::size_t a = 0; a < std::size(kAlgorithms); ++a) dithers[a].init(kAlgorithms[a], 4, 99);
    // The resampler is counted from its first chunk, not warmed: r8brain sizes every buffer it uses in its
    // constructor, which `init` runs, so not even the first `process` may allocate.
    rp::dsp::Resampler resampler;
    resampler.init(44100.0, 11468.0);

    rp::test::beginCountingAllocations(true);
    for (int block = 0; block < 8; ++block) {
        const float* src = in.data() + (block % 4) * 1024;
        gain.process(src, out.data(), 1024);
        (void)rp::dsp::peak(src, 1024);
        biquad.process(src, out.data(), 1024);
        for (auto& d : dithers) {
            d.process(src, codes.data(), 1024);
            d.processLevels(src, out.data(), 1024);
        }
        (void)rp::dsp::inputSeed(src, 1024);
        std::size_t taken = 0;
        while (taken < 1024) {
            taken += resampler.push(src + taken, 1024 - taken);
            resampler.pull(out.data(), kFrames);
        }
        resampler.pushSilence(300);
        resampler.pull(out.data(), 7);
    }
    const auto counted = rp::test::endCountingAllocations();
    INFO("C allocations counted: " << rp::test::countsCAllocations());
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
}
