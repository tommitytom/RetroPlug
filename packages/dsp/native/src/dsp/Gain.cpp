#include "dsp/Gain.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {

void Gain::process(const float* in, float* out, std::size_t frames) const noexcept {
    for (std::size_t i = 0; i < frames; ++i) out[i] = in[i] * gain;
}

float peak(const float* in, std::size_t frames) noexcept {
    float maxAmp = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) maxAmp = std::max(maxAmp, std::abs(in[i]));
    return maxAmp;
}

float normalizedGain(float gain, float peakLevel) noexcept {
    if (peakLevel > 0.0f) gain *= 1.0f / peakLevel;
    return gain;
}

} // namespace rp::dsp
