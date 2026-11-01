#pragma once

// <algorithm> and <cmath> are kept for whoever reaches them through this header: host/rpc/BackendTypes.hpp re-exports
// it across the plugin's DSP surface, and kPi's only caller is test/audio/Dither.test.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <rfl/Literal.hpp>
#include <rfl/TaggedUnion.hpp>

// Sample-processing chain for LSDJ kit compilation. Ported from
// the legacy build's Effects.h + AudioDithering.h.
//
// Each effect is a plain-data struct that round-trips via reflectcpp; the
// `LsdjEffect` TaggedUnion below is the serialized effect descriptor, which the
// compileKit / compileDmc RPCs decode. The gain and filter `processEffect(...)`
// overloads run in `KitUtil::compileSample` and `RisaDmcCodec::encode` against a
// float buffer in [-1, 1]; the dither one is the 4-bit codes as floats, which
// `KitUtil::compileSample` gets from `quantise` instead, so its only caller is
// test/audio/Dither.test.cpp. All of them are whole-buffer loops over the
// rp::dsp kernels (src/dsp/), which hold the arithmetic; this file only maps an
// effect's fields onto a kernel's.

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

// With `normalize`, the whole buffer's peak is measured first and folded into
// the gain (rp::dsp::peak, rp::dsp::normalizedGain); then one multiply.
void processEffect(const GainEffect& effect,
                   std::vector<float>& target,
                   float sampleRate);

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
// Output : samples already quantised into [0, 2^bitDepth - 1] floats (the
//          codes `quantise` below gives, as floats).
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

// LSDj's 4-bit sample codes (0..15) for a whole buffer at the kit rate: through
// `dither`'s algorithm, or plain rounding when there is none. The TPDF
// algorithms are seeded from the whole buffer (rp::dsp::inputSeed), so the
// codes are a function of the buffer and the effect alone.
std::vector<std::uint16_t> quantise(const std::vector<float>& buffer,
                                    const DitherEffect* dither);

// `LsdjEffect` is the on-disk variant; serialised under the discriminator
// key `"type"` to match the legacy spelling.
using LsdjEffect = rfl::TaggedUnion<"type", GainEffect, FilterEffect, DitherEffect>;

} // namespace rp::lsdj
