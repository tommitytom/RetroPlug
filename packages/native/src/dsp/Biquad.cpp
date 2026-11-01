#include "dsp/Biquad.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {

namespace {

// A `float` pi, as every coefficient below is computed in `float`.
constexpr float kPi = 3.14159265358979323846f;

BiquadCoefficients lowPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - cw) / (2.0f * a0);
    s.b1 = (1.0f - cw) / a0;
    s.b2 = (1.0f - cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients highPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  (1.0f + cw) / (2.0f * a0);
    s.b1 = -(1.0f + cw) / a0;
    s.b2 =  (1.0f + cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients bandPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  sw / (2.0f * a0);
    s.b1 = 0.0f;
    s.b2 = -sw / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients bandStop(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = 1.0f / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = 1.0f / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients peaking(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha / A;
    s.b0 = (1.0f + alpha * A) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f - alpha * A) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha / A) / a0;
    return s;
}

BiquadCoefficients lowShelf(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float beta = std::sqrt(A) / p.q;
    const float a0 = (A + 1.0f) + (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) + (A - 1.0f) * cw - beta * sw) / a0;
    return s;
}

BiquadCoefficients highShelf(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float beta = std::sqrt(A) / p.q;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) - (A - 1.0f) * cw - beta * sw) / a0;
    return s;
}

BiquadCoefficients allPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - alpha) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f + alpha) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

} // namespace

BiquadCoefficients biquadCoefficients(const BiquadSettings& s) noexcept {
    switch (s.type) {
        case BiquadType::LowPass:   return lowPass(s);
        case BiquadType::HighPass:  return highPass(s);
        case BiquadType::BandPass:  return bandPass(s);
        case BiquadType::BandStop:  return bandStop(s);
        case BiquadType::Peak:      return peaking(s);
        case BiquadType::LowShelf:  return lowShelf(s);
        case BiquadType::HighShelf: return highShelf(s);
        case BiquadType::AllPass:   return allPass(s);
    }
    // A value outside the enum (a cast, not a parse: reflect-cpp refuses an unknown name) passes the signal through.
    return {};
}

void Biquad::process(const float* in, float* out, std::size_t frames) noexcept {
    float sx1 = x1, sx2 = x2, sy1 = y1, sy2 = y2;
    for (std::size_t i = 0; i < frames; ++i) {
        const float x = in[i];
        const float y = c.b0 * x + c.b1 * sx1 + c.b2 * sx2
                      - c.a1 * sy1 - c.a2 * sy2;
        sx2 = sx1; sx1 = x;
        sy2 = sy1; sy1 = y;
        out[i] = std::clamp(y, -1.0f, 1.0f);
    }
    x1 = sx1; x2 = sx2; y1 = sy1; y2 = sy2;
}

} // namespace rp::dsp
