#pragma once

#include <cmath>

// Tiny dependency-free one-pole exponential value smoother. API mirrors DPF's
// ExponentialValueSmoother so call sites that previously used the DPF version
// continue to work; the implementation is independent so non-DPF embedders
// (CLI, future hosts) don't pull DPF headers in.
class ExpSmoother {
public:
    void setSampleRate(float newSampleRate) noexcept {
        sampleRate_ = newSampleRate > 0.0f ? newSampleRate : 44100.0f;
        recomputeCoef();
    }

    // Time constant in seconds (T60-equivalent ~ 4*tau for 0.018 of target).
    void setTimeConstant(float seconds) noexcept {
        tau_ = seconds > 0.0f ? seconds : 0.001f;
        recomputeCoef();
    }

    void setTargetValue(float v) noexcept { target_ = v; }
    void clearToTargetValue()    noexcept { current_ = target_; }

    inline float next() noexcept {
        current_ += (target_ - current_) * coef_;
        return current_;
    }

private:
    void recomputeCoef() noexcept {
        coef_ = 1.0f - std::exp(-1.0f / (tau_ * sampleRate_));
    }

    float sampleRate_ = 44100.0f;
    float tau_        = 0.020f; // 20 ms default
    float target_     = 0.0f;
    float current_    = 0.0f;
    float coef_       = 0.0f;
};
