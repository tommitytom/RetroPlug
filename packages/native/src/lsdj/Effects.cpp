#include "lsdj/Effects.hpp"

#include <cmath>

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

// ----- Dither algorithms (ported from old/src/core/audio/AudioDithering.h) -----

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

void highPassTPDF(std::vector<float>& buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    constexpr float quantum = 1.0f;

    // Per-call RNG so two parallel sample compiles don't share state. The
    // resulting non-reproducibility is the same as the legacy code: dither
    // bytes change run-to-run but the audible result is indistinguishable.
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float prevDither   = 0.0f;
    float shapingState = 0.0f;

    for (float& v : buf) {
        const float dither = (dist(rng) + dist(rng) - 1.0f) * quantum * 0.5f;
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

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float err1 = 0.0f, err2 = 0.0f;
    for (float& v : buf) {
        const float dither = (dist(rng) + dist(rng) - 1.0f) * quantum * 0.5f;
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
