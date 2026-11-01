#include "lsdj/Effects.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

#include "util/Hash.hpp"

namespace rp::lsdj {

namespace {

// Biquad state. Coefficients are normalized to a0 = 1.0 in the calculators
// below — the divisions happen during coefficient setup, so the inner loop
// is a straight transposed-direct-form-1 implementation.
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

// ----- Dither algorithms (ported from the legacy build's AudioDithering.h) -----

void errorDiffusion(std::vector<float>& buf, int bitDepth) {
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
}

void sierraLite(std::vector<float>& buf, int bitDepth) {
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
}

void jjnErrorDiffusion(std::vector<float>& buf, int bitDepth) {
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
}

// One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
// `DitherType`'s ordinal, so the order the enum happens to declare its algorithms in cannot re-seed a kit that uses
// one of them.
constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fnv1a64("HighPassTPDF")
constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fnv1a64("ShapedTPDF")

// The buffer's samples as the bytes FNV-1a hashes, least-significant byte of each float first whatever the host's
// own byte order, so a big-endian build seeds the same as a little-endian one. A float is IEEE-754 binary32 on every
// target; on a little-endian host the bytes are already in that order, so they are hashed in place.
std::uint64_t hashFloats(const std::vector<float>& buf) {
    if constexpr (std::endian::native == std::endian::little) {
        return rp::hash::fnv1a64(reinterpret_cast<const std::uint8_t*>(buf.data()),
                                 buf.size() * sizeof(float));
    } else {
        std::vector<std::uint8_t> bytes(buf.size() * sizeof(float));
        for (std::size_t i = 0; i < buf.size(); ++i) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(buf[i]);
            for (std::size_t b = 0; b < sizeof(float); ++b) {
                bytes[i * sizeof(float) + b] = static_cast<std::uint8_t>(bits >> (8 * b));
            }
        }
        return rp::hash::fnv1a64(bytes.data(), bytes.size());
    }
}

// A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
// mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
// three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
float uniform01(std::mt19937& rng) {
    return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
}

// The seed both TPDF dithers draw their noise from: `hashFloats` over the buffer this pass quantises, folded to
// `mt19937`'s 32-bit word and salted with the algorithm, so the two dithers over one buffer do not run the same
// sequence. Drawing it from the input is what makes a compile reproducible: a kit bank is a file, and two compiles of
// the same sample have to be the same file. The seed is a function of the buffer's contents and the algorithm only,
// not of the bit depth or any other setting.
//
// The input is the buffer the dither is handed, which for `KitUtil::compileSample` is the windowed, post-effect,
// resampled sample at the Game Boy kit rate (11468 Hz) as the dither receives it, BEFORE `frameAlign` trims it to
// whole frames - not the source-rate window the gain and filter see, and not the trimmed bank.
//
// Same input, same noise on any host: the draw and the hash are both specified, so the dither's own arithmetic is
// exact wherever a float is binary32. A whole bank is byte-identical only where the resampler and the filters agree
// too, which is why exact kit bytes are compared on one reference target.
std::mt19937 ditherRng(const std::vector<float>& buf, std::uint32_t salt) {
    const std::uint64_t hash = hashFloats(buf);
    const std::uint32_t folded = static_cast<std::uint32_t>(hash ^ (hash >> 32));
    return std::mt19937(folded ^ salt);
}

void highPassTPDF(std::vector<float>& buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    constexpr float quantum = 1.0f;

    // A per-call RNG so two parallel sample compiles don't share state, seeded from the input so they agree.
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
}

void shapedTPDF2ndOrder(std::vector<float>& buf, int bitDepth) {
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
}

} // namespace

void processEffect(const FilterEffect& effect,
                   std::vector<float>& target,
                   float sampleRate) {
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
}

void processEffect(const DitherEffect& effect,
                   std::vector<float>& target,
                   float /*sampleRate*/) {
    constexpr int kBitDepth = 4;  // LSDJ kit samples are 4-bit
    switch (effect.ditherType) {
        case DitherType::HighPassTPDF:   highPassTPDF       (target, kBitDepth); break;
        case DitherType::ShapedTPDF:     shapedTPDF2ndOrder (target, kBitDepth); break;
        case DitherType::ErrorDiffusion: errorDiffusion     (target, kBitDepth); break;
        case DitherType::JJN:            jjnErrorDiffusion  (target, kBitDepth); break;
        case DitherType::SierraLite:     sierraLite         (target, kBitDepth); break;
    }
}

} // namespace rp::lsdj
