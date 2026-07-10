#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <rfl/Literal.hpp>
#include <rfl/TaggedUnion.hpp>

// Sample-processing chain for LSDJ kit compilation. Ported from
// old/src/core/Effects.h + old/src/core/audio/AudioDithering.h.
//
// Each effect is a plain-data struct that round-trips via reflectcpp; the
// `LsdjEffect` TaggedUnion below is the serialized effect descriptor. The
// per-effect `processEffect(...)` overloads run in `KitUtil::compileKit`
// against a float buffer in [-1, 1] (or, for dither, expect [-1, 1] and
// output samples pre-quantized into [0, 2^bitDepth - 1]).

namespace rp::lsdj {

inline constexpr float kPi = 3.14159265358979323846f;

// -----------------------------------------------------------------------
// GainEffect — uniform amplification with optional peak-normalisation.
// -----------------------------------------------------------------------
struct GainEffect {
    using Tag = rfl::Literal<"gain">;

    bool  normalize = true;
    float gain      = 1.0f;
};

inline void processEffect(const GainEffect& effect,
                          std::vector<float>& target,
                          float /*sampleRate*/) {
    float gain = effect.gain;
    if (effect.normalize) {
        float maxAmp = 0.0f;
        for (float v : target) maxAmp = std::max(maxAmp, std::abs(v));
        if (maxAmp > 0.0f) gain *= 1.0f / maxAmp;
    }
    for (float& v : target) v *= gain;
}

// -----------------------------------------------------------------------
// FilterEffect — RBJ-cookbook biquads.
// -----------------------------------------------------------------------
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
    using Tag = rfl::Literal<"filter">;

    FilterType filterType = FilterType::LowPass;
    float      frequency  = 5734.0f;   // GAMEBOY_SAMPLE_RATE / 2; nyquist for 11468 Hz
    float      q          = 1.0f;
    float      gain       = 0.0f;      // dB, used by Peak / LowShelf / HighShelf
};

void processEffect(const FilterEffect& effect,
                   std::vector<float>& target,
                   float sampleRate);

// -----------------------------------------------------------------------
// DitherEffect — bit-depth reduction with noise shaping.
//
// Input  : samples in [-1, 1].
// Output : samples already quantised into [0, 2^bitDepth - 1] floats — the
//          kit-compile pipeline passes the buffer straight to
//          SampleUtil::convertScaledF32ToNibbles after the dither pass.
//
// Five algorithms preserved verbatim from the legacy AudioDithering helpers.
// `HighPassTPDF` is the LSDJ legacy default.
// -----------------------------------------------------------------------
enum class DitherType : std::uint32_t {
    HighPassTPDF   = 0,
    ShapedTPDF     = 1,
    ErrorDiffusion = 2,
    JJN            = 3,
    SierraLite     = 4,
};

struct DitherEffect {
    using Tag = rfl::Literal<"dither">;

    DitherType ditherType = DitherType::HighPassTPDF;
};

void processEffect(const DitherEffect& effect,
                   std::vector<float>& target,
                   float sampleRate);

// `LsdjEffect` is the on-disk variant; serialised under the discriminator
// key `"type"` to match the legacy spelling.
using LsdjEffect = rfl::TaggedUnion<"type", GainEffect, FilterEffect, DitherEffect>;

} // namespace rp::lsdj
