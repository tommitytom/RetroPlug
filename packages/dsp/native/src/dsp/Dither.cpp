#include "dsp/Dither.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "util/Hash.hpp"

namespace rp::dsp {

// The hash and its big-endian branch both read a float as IEEE-754 binary32, which is what makes a seed a function of
// the samples and not of a representation a target happens to pick. Fail the build rather than re-seed every kit.
static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
              "the dither seed hashes a float's binary32 bytes");

namespace {

// The standard's check of mt19937 ([rand.predef]): the 10000th draw of a default-constructed engine. Held here so the
// engine the kit's noise comes from is the one the standard specifies, draw for draw.
constexpr bool tenThousandthDrawIsTheStandards() {
    Mt19937 engine;
    for (int i = 0; i < 9999; ++i) engine();
    return engine() == 4123659995u;
}
static_assert(tenThousandthDrawIsTheStandards(), "Mt19937 is not the standard's mt19937");

// One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
// the enum's ordinal, so the order `DitherAlgorithm` (or the kit effect's `DitherType`) declares its algorithms in
// cannot re-seed a kit that uses one of them.
constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fold(fnv1a64("HighPassTPDF"))
constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fold(fnv1a64("ShapedTPDF"))

// A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
// mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
// three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
float uniform01(Mt19937& rng) noexcept {
    return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
}

// A rounded value as its code. The value is integral and in [0, maxValue] for any input but a NaN, which the clamp
// passes through and which reads as code 0 here rather than as an undefined conversion.
std::uint16_t toCode(float q) noexcept {
    return q >= 0.0f ? static_cast<std::uint16_t>(q) : std::uint16_t{0};
}

// A code as audio. The caller passes `maxValue` because the dither loop has it already: `codeToLevel` recomputes it
// from the bit depth, but a per-sample recompute there would be wasted work in `processLevels`.
float levelOf(std::uint32_t code, float maxValue) noexcept {
    return (static_cast<float>(code) / maxValue) * 2.0f - 1.0f;
}

} // namespace

// Same input, same seed on any host: the hash reads each float's binary32 bytes least-significant first, which on a
// little-endian host is their order in memory, so they are hashed in place; elsewhere each float is reordered into
// four bytes and the hash continued over them, which gives the same value without a copy of the buffer.
std::uint32_t inputSeed(const float* in, std::size_t frames) noexcept {
    std::uint64_t hash = rp::hash::kFnv1a64Basis;
    if constexpr (std::endian::native == std::endian::little) {
        hash = rp::hash::fnv1a64(reinterpret_cast<const std::uint8_t*>(in), frames * sizeof(float));
    } else {
        for (std::size_t i = 0; i < frames; ++i) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(in[i]);
            const std::uint8_t bytes[4] = {
                static_cast<std::uint8_t>(bits),       static_cast<std::uint8_t>(bits >> 8),
                static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 24),
            };
            hash = rp::hash::fnv1a64(bytes, sizeof(bytes), hash);
        }
    }
    return static_cast<std::uint32_t>(hash ^ (hash >> 32));
}

float codeToLevel(std::uint32_t code, int bits) noexcept {
    bits = std::clamp(bits, kMinDitherBits, kMaxDitherBits);
    return levelOf(code, static_cast<float>((1 << bits) - 1));
}

void Dither::init(DitherAlgorithm algo, int bitDepth, std::uint32_t seed) noexcept {
    algorithm = algo;
    bits      = std::clamp(bitDepth, kMinDitherBits, kMaxDitherBits);
    maxValue  = static_cast<float>((1 << bits) - 1);
    halfScale = maxValue / 2.0f;

    std::uint32_t salt = 0;
    if (algo == DitherAlgorithm::HighPassTPDF) salt = kHighPassTPDFSalt;
    if (algo == DitherAlgorithm::ShapedTPDF) salt = kShapedTPDFSalt;
    rng.seed(seed ^ salt);

    prevDither = shapingState = 0.0f;
    err1 = err2 = 0.0f;
    error = 0.0f;
    for (float& e : past) e = 0.0f;
    pastCount = 0;
}

// Each algorithm is the kit path's loop over the stream, with what it carries from one sample to the next kept in the
// struct rather than in locals. Sierra Lite and JJN are forward diffusions, whose legacy form runs over a padded copy
// of the buffer and adds each sample's error into the samples ahead of it; a sample's value there is its scaled input
// plus the errors of the samples behind it, added oldest first. Keeping those errors (`past`, newest first) and adding
// them oldest first gives the same float sums in the same order with no copy, so no allocation and any block split
// (Dsp.test.cpp holds the two forms equal). A term for a sample before the stream's first is skipped rather than added
// as zero, as the padded form has none to add.
template <class Emit>
void Dither::run(const float* in, std::size_t frames, Emit emit) noexcept {
    constexpr float quantum = 1.0f;

    switch (algorithm) {
        case DitherAlgorithm::None:
            for (std::size_t i = 0; i < frames; ++i) {
                const float v = (std::clamp(in[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
                emit(i, std::round(v * maxValue));
            }
            break;

        case DitherAlgorithm::HighPassTPDF:
            for (std::size_t i = 0; i < frames; ++i) {
                const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
                float shaped = dither - prevDither * 0.94f;
                prevDither = dither;
                shaped -= shapingState * 0.5f;
                float value = (in[i] + 1.0f) * halfScale + shaped;
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                shapingState = q - value;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::ShapedTPDF:
            for (std::size_t i = 0; i < frames; ++i) {
                const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
                // Coefficients tuned for ~11 kHz target rate (LSDJ kit sample rate)
                float value = (in[i] + 1.0f) * halfScale + dither + (1.623f * err1) - (0.982f * err2);
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                const float qe = q - value;
                err2 = err1;
                err1 = qe;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::ErrorDiffusion:
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                value += error * 0.9f;
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                error = value - q;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::JJN: {
            constexpr float w1 = 7.0f / 48.0f;
            constexpr float w2 = 5.0f / 48.0f;
            constexpr float w3 = 3.0f / 48.0f;
            constexpr float w4 = 1.0f / 48.0f;
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                if (pastCount >= 4) value += past[3] * w4;
                if (pastCount >= 3) value += past[2] * w3;
                if (pastCount >= 2) value += past[1] * w2;
                if (pastCount >= 1) value += past[0] * w1;
                const float q = std::round(std::clamp(value, 0.0f, maxValue));
                past[3] = past[2]; past[2] = past[1]; past[1] = past[0];
                past[0] = value - q;
                if (pastCount < 4) ++pastCount;
                emit(i, q);
            }
            break;
        }

        case DitherAlgorithm::SierraLite:
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                if (pastCount >= 2) value += past[1] * 0.25f;
                if (pastCount >= 1) value += past[0] * 0.5f;
                const float q = std::round(std::clamp(value, 0.0f, maxValue));
                past[1] = past[0];
                past[0] = value - q;
                if (pastCount < 2) ++pastCount;
                emit(i, q);
            }
            break;

        default:
            // A value outside the enum (a cast, never a parse) quantises as plain rounding would, rather than leaving
            // the output unwritten.
            for (std::size_t i = 0; i < frames; ++i) {
                const float v = (std::clamp(in[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
                emit(i, std::round(v * maxValue));
            }
            break;
    }
}

void Dither::process(const float* in, std::uint16_t* codes, std::size_t frames) noexcept {
    run(in, frames, [codes](std::size_t i, float q) noexcept { codes[i] = toCode(q); });
}

void Dither::processLevels(const float* in, float* out, std::size_t frames) noexcept {
    const float maxValue_ = maxValue;
    run(in, frames, [out, maxValue_](std::size_t i, float q) noexcept { out[i] = levelOf(toCode(q), maxValue_); });
}

} // namespace rp::dsp
