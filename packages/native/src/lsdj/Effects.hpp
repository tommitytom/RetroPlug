#pragma once

#include <cstdint>
#include <vector>

// The kit path's sample-processing effects, as whole-buffer wrappers. Ported from the legacy build's Effects.h +
// AudioDithering.h.
//
// The gain and filter `processEffect(...)` overloads take a float buffer in [-1, 1] at the rate the chain runs them
// at; the dither one takes the kit rate's buffer and leaves the 4-bit codes as floats, through `quantise` below. All
// of them are whole-buffer loops over the rp::dsp kernels (src/dsp/), which hold the arithmetic; this file only maps
// an effect's fields onto a kernel's. Nothing serialises these structs: a kit compile is a chain of `rp.dsp` nodes, and
// the wrappers are the kit-path reference the tests hold to, bit for bit: test/graph/DspBufferNodes.test.cpp holds the
// nodes to them, and test/audio/Dither.test.cpp holds the dither effect's seed.

namespace rp::lsdj {

inline constexpr float kPi = 3.14159265358979323846f;

// -----------------------------------------------------------------------
// GainEffect — uniform amplification with optional peak-normalisation.
// -----------------------------------------------------------------------
struct GainEffect {
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

} // namespace rp::lsdj
