#pragma once

// The biquad kernel: the RBJ cookbook's eight filters, in `float`.
//
// The arithmetic is the kit path's to the bit (a kit bank's bytes depend on it): each coefficient set is computed in
// `float` with a `float` pi and normalised to a0 = 1 at setup, the loop is direct form I (two input and two output
// delays) with its feedback taken BEFORE the output clamp, and the output is clamped to [-1, 1].

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

// The eight filters. The values are the kit effect's `FilterType`'s, which lsdj/Effects.cpp checks at compile time.
enum class BiquadType : std::uint32_t {
    LowPass   = 0,
    HighPass  = 1,
    BandPass  = 2,
    BandStop  = 3,
    Peak      = 4,
    LowShelf  = 5,
    HighShelf = 6,
    AllPass   = 7,
};

// One filter's shape: `gainDb` is read by Peak and the two shelves only.
struct BiquadSettings {
    BiquadType type       = BiquadType::LowPass;
    float      sampleRate = 48000.0f;
    float      frequency  = 5734.0f;
    float      q          = 1.0f;
    float      gainDb     = 0.0f;
};

// Normalised to a0 = 1.
struct BiquadCoefficients {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

BiquadCoefficients biquadCoefficients(const BiquadSettings& s) noexcept;

// One channel's filter. `init` computes the coefficients and clears the state; `setup` recomputes the coefficients
// and keeps the state, which is what a filter being swept between blocks wants.
struct Biquad {
    BiquadCoefficients c;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void init(const BiquadSettings& s) noexcept {
        setup(s);
        reset();
    }
    void setup(const BiquadSettings& s) noexcept { c = biquadCoefficients(s); }
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    // `in` and `out` may be the same buffer.
    void process(const float* in, float* out, std::size_t frames) noexcept;
};

} // namespace rp::dsp
