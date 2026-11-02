#include "dsp/Dpcm.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {
namespace {

// 63 * 10^(-3dB/20), rounded: wav2dmc's NORMALIZE_TARGET_SWING, the 7-bit map's swing when normalising.
constexpr long kNormalizeSwing = 45;

// The DC blocker's pole, one minus one 256th: a high-pass at a fraction of the rate, which is what wav2dmc uses.
constexpr double kDcBlockR = 255.0 / 256.0;

// Floor division toward negative infinity (b > 0). C integer division truncates toward zero, which diverges from
// wav2dmc's Python `//` on a negative numerator: load-bearing for the 7-bit map.
long floorDiv(long a, long b) noexcept {
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// The int16 domain sample: the float scaled by 32767, clamped to the int16 range and rounded half away from zero.
// wav2dmc works on int16 and risa's codec has always converted through this exact expression, so it is the encoder's
// first stage rather than an approximation of it.
std::int32_t toInt16Domain(float sample) noexcept {
    const float v = std::clamp(sample * 32767.0f, -32768.0f, 32767.0f);
    return static_cast<std::int32_t>(std::lround(v));
}

// The DC blocker, one sample per call: `y = x - x1 + R * y1`, truncated toward zero and clamped to int16. Its state is
// the last input and the last unclamped output, which is what makes the second pass over the buffer reproduce the
// first one's samples exactly: both loops inline this one expression, so they agree unless a compiler contracts the
// multiply into a fused operation in one inlined copy and not the other (GCC's default -ffp-contract=fast allows that
// on a target with an FMA). That identity is the assumption the two-pass shape rests on, and exact kit bytes are
// compared on one reference target only.
struct DcBlocker {
    double x1 = 0.0;
    double y1 = 0.0;

    long operator()(std::int32_t s) noexcept {
        const double x = static_cast<double>(s);
        const double y = x - x1 + kDcBlockR * y1;
        x1 = x;
        y1 = y;
        return std::clamp(static_cast<long>(y), -32768L, 32767L);  // the cast truncates toward zero
    }
};

} // namespace

void encodeDpcm(const float* in, std::size_t frames, bool normalize, std::uint8_t* bits) noexcept {
    // The peak over the DC-blocked samples, which the normalising map divides by. A floor of 1 is the codec's: silence
    // still divides by something, and by the same 1 on every host.
    long peak = 1;
    {
        DcBlocker dc;
        for (std::size_t i = 0; i < frames; ++i) {
            const long v = dc(toInt16Domain(in[i]));
            peak = std::max(peak, std::abs(v));
        }
    }

    // The 7-bit map straight into the 1-bit delta: the counter starts at 64, a sample at or above it is a step up and
    // moves it two closer to 127, one below it a step down and two closer to 0. Both stops are the hardware's, and the
    // guards rather than a clamp are the deliberate quirk risa's decoder mirrors.
    DcBlocker dc;
    int       counter = 64;
    for (std::size_t i = 0; i < frames; ++i) {
        const long v = dc(toInt16Domain(in[i]));
        const long q = 64 + (normalize ? floorDiv(v * kNormalizeSwing, peak) : floorDiv(v * 63, 32768));
        const long u7 = std::clamp(q, 0L, 127L);
        if (u7 >= counter) {
            bits[i] = 1;
            if (counter < 127) counter += 2;
        } else {
            bits[i] = 0;
            if (counter > 0) counter -= 2;
        }
    }
}

} // namespace rp::dsp
