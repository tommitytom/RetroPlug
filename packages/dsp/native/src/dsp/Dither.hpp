#pragma once

// The dither / quantise kernel: audio in [-1, 1] to integer codes 0 .. 2^bits - 1.
//
// Six algorithms. `None` is plain rounding, which is what the kit path does when no dither is set; the other five are
// the kit effect's, ported from the legacy build's AudioDithering helpers. Their arithmetic is the kit path's to the
// bit: a code is `round(clamp(value, 0, 2^bits - 1))` of the input scaled by `(v + 1) * (2^bits - 1) / 2` plus that
// algorithm's error or noise term. The two TPDF algorithms draw their noise from an `mt19937` seeded once, at `init`,
// so the codes are a function of the input, the algorithm, the bit depth and the seed alone.
//
// Streaming: every algorithm's state is a few floats plus the noise generator (the error-diffusion forms keep their
// past errors, one for ErrorDiffusion, two for Sierra Lite and four for JJN, rather than a padded copy of the buffer),
// so any split of a stream into blocks gives the codes one call over the whole of it gives.

#include <cstddef>
#include <cstdint>
#include <random>

namespace rp::dsp {

enum class DitherAlgorithm : std::uint32_t {
    None           = 0,  // round(((clamp(v, -1, 1) + 1) * 0.5) * (2^bits - 1))
    HighPassTPDF   = 1,  // TPDF noise high-passed, with first-order error feedback (the LSDj kit default)
    ShapedTPDF     = 2,  // TPDF noise with a second-order error-shaping filter tuned for ~11 kHz
    ErrorDiffusion = 3,  // one error term, fed back at 0.9
    JJN            = 4,  // Jarvis-Judice-Ninke's weights along one dimension: 7, 5, 3, 1 / 48 over four samples
    SierraLite     = 5,  // Sierra Lite's along one dimension: 1/2, 1/4 over two samples
};

inline constexpr int kMinDitherBits = 1;
inline constexpr int kMaxDitherBits = 16;

// The seed the kit path draws its noise from: FNV-1a 64 over the samples' IEEE-754 bytes, least-significant byte of
// each float first whatever the host's byte order, folded to 32 bits. A function of the samples alone; `init` salts it
// with the algorithm. The kit path hashes the whole buffer the dither quantises (the resampled sample at the kit rate,
// before it is trimmed to whole frames), so a driver that runs the kernel over that buffer in blocks hashes all of it
// with this first, and passes the result to `init`. Allocates nothing.
std::uint32_t inputSeed(const float* in, std::size_t frames) noexcept;

// What a code means as audio: `(code / (2^bits - 1)) * 2 - 1`, the level the LSDj kit decoder gives a 4-bit code, so
// codes mapped back through this are the quantised signal (a bit crusher's output).
float codeToLevel(std::uint32_t code, int bits) noexcept;

struct Dither {
    DitherAlgorithm algorithm = DitherAlgorithm::None;
    int             bits      = 4;
    float           maxValue  = 15.0f;  // 2^bits - 1
    float           halfScale = 7.5f;   // maxValue / 2

    std::mt19937 rng;                  // the TPDF noise, seeded at init
    float        prevDither   = 0.0f;  // HighPassTPDF
    float        shapingState = 0.0f;  // HighPassTPDF
    float        err1 = 0.0f, err2 = 0.0f;  // ShapedTPDF: the last two quantisation errors, newest first
    float        error = 0.0f;              // ErrorDiffusion
    float        past[4] = {};              // JJN / Sierra Lite: the last errors, newest first
    std::uint32_t pastCount = 0;            // how many of `past` the stream has produced yet (at most 4)

    // `bits` is clamped to [kMinDitherBits, kMaxDitherBits]. `seed` is read by the two TPDF algorithms only; each
    // salts it with its own constant, so the two over one buffer and one seed do not draw the same noise.
    void init(DitherAlgorithm algo, int bitDepth, std::uint32_t seed) noexcept;

    // Codes out, 0 .. 2^bits - 1.
    void process(const float* in, std::uint16_t* codes, std::size_t frames) noexcept;

    // The same codes as audio levels (codeToLevel), for a node that outputs audio. `in` and `out` may be the same
    // buffer.
    void processLevels(const float* in, float* out, std::size_t frames) noexcept;

private:
    template <class Emit>
    void run(const float* in, std::size_t frames, Emit emit) noexcept;
};

} // namespace rp::dsp
