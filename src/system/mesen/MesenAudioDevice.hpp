#pragma once

#include "Core/Shared/Interfaces/IAudioDevice.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// IAudioDevice implementation that captures rendered samples into an
// in-process ring buffer. Mesen's SoundMixer feeds us int16 stereo at the
// rate we ask for; MesenNesSystem drains them as float32 once enough samples
// for the current audio block have accumulated.
//
// Lifted from old/src/mesen/MesenAudioDevice.h verbatim (no orb deps to fix).
class MesenAudioDevice final : public IAudioDevice {
public:
    void PlayBuffer(int16_t* samples, uint32_t count, uint32_t /*sampleRate*/, bool /*isStereo*/) override {
        const std::size_t base = buffer_.size();
        buffer_.resize(base + static_cast<std::size_t>(count) * 2);
        std::memcpy(buffer_.data() + base, samples, static_cast<std::size_t>(count) * 2 * sizeof(int16_t));
    }

    void Stop() override {}
    void Pause() override {}
    void ProcessEndOfFrame() override {}
    string GetAvailableDevices() override { return {}; }
    void SetAudioDevice(string) override {}
    AudioStatistics GetStatistics() override { return {}; }

    std::size_t availableFrames() const {
        return buffer_.size() / 2;
    }

    // Drain up to `frameCount` stereo frames into `dest` as normalised float32.
    // Returns the number of frames actually written.
    std::uint32_t drain(float* dest, std::uint32_t frameCount) {
        const std::uint32_t have = static_cast<std::uint32_t>(buffer_.size() / 2);
        const std::uint32_t take = std::min(have, frameCount);
        constexpr float kScale = 1.0f / 32768.0f;
        for (std::uint32_t i = 0; i < take * 2; ++i) {
            dest[i] = buffer_[i] * kScale;
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + take * 2);
        return take;
    }

private:
    std::vector<int16_t> buffer_;
};
