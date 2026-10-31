#pragma once

#include "Core/Shared/Interfaces/IAudioDevice.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

// IAudioDevice implementation that captures rendered samples into a fixed-capacity ring of interleaved stereo int16.
// Mesen's SoundMixer feeds us int16 stereo at the rate we ask for; a Mesen system drains them as float32 once enough
// samples for the current audio block have accumulated, and the ring's FILL is that core's block clock
// (availableFrames), so the step loops and the N8 FIFO's scheduling keep stepping on the same number.
//
// The ring never allocates on the block thread. Its capacity is one host block plus the most a single PlayBuffer can
// carry: Mesen's mixer resamples into a fixed 0x10000-int16 buffer and hands the device at most 0x10000 / 2 frames
// per call (SoundMixer::PlayAudioBuffer), on every console and at every sample rate, and a step loop that gates on
// `availableFrames() >= framesNeeded` stops with at most one such flush over a block. A system node sizes the ring
// from the plan's maximum block before its core runs its first block, and again when a host grows its block, which
// reactivates the graph while it is quiescent (reserveAudio).
class MesenAudioDevice final : public IAudioDevice {
public:
    // The most frames Mesen's SoundMixer hands over in one PlayBuffer. It is the mixer's own output buffer, not a
    // property of a console or a sample rate: the resampler writes into _sampleBuffer (0x10000 int16, stereo) and
    // never past it, and a pitch-adjusted flush goes through the smaller 0x8000 one. So this bound holds for every
    // core RetroPlug builds.
    static constexpr std::uint32_t kMaxPlayBufferFrames = 0x10000 / 2;

    // The engine's default plan size (Engine::kDefaultMaxBlockFrames), so a core built outside a plan (a test, a tool)
    // runs blocks the same size an engine starts at. A system node replaces it with the plan's maximum at activation.
    static constexpr std::uint32_t kDefaultBlockFrames = 8192;

    MesenAudioDevice() { reserveAudio(kDefaultBlockFrames); }

    // Size the ring for blocks of up to `maxBlockFrames`, keeping the frames it already holds in order. Grows only:
    // a plan's maximum only ever grows (Engine::setMaxBlockFrames), and keeping a larger ring costs nothing but the
    // memory it already holds. Control thread or activation, quiescent: neither ever runs with a block in flight.
    void reserveAudio(std::uint32_t maxBlockFrames) {
        // A block anywhere near this large could not be planned, but the sum must not wrap to a smaller capacity that
        // the growth check below would then swallow.
        assert(maxBlockFrames <= UINT32_MAX - kMaxPlayBufferFrames);
        const std::uint32_t want = maxBlockFrames + kMaxPlayBufferFrames;
        if (want <= capacity_) return;
        std::vector<int16_t> grown(static_cast<std::size_t>(want) * 2);
        // The queued frames are one contiguous run in ring order, so at most two copies carry them across.
        if (count_ > 0) {
            const std::uint32_t first = std::min(count_, capacity_ - head_);
            std::memcpy(grown.data(), buffer_.data() + static_cast<std::size_t>(head_) * 2,
                        static_cast<std::size_t>(first) * 2 * sizeof(int16_t));
            std::memcpy(grown.data() + static_cast<std::size_t>(first) * 2, buffer_.data(),
                        static_cast<std::size_t>(count_ - first) * 2 * sizeof(int16_t));
        }
        buffer_.swap(grown);
        capacity_ = want;
        head_     = 0;
    }

    // Append `count` stereo frames at the tail. A write past capacity drops the NEWEST frames (the ones that do not
    // fit) and counts them: the frames already queued are the ones the pending block's sample indices, and the N8
    // FIFO's scheduled bytes, already refer to, and they are what `availableFrames()` has told the step loop it has.
    // Overwriting them would move every sample after the lost ones earlier in the block. The capacity makes this
    // unreachable; a debug build asserts, a release build counts and plays on.
    void PlayBuffer(int16_t* samples, uint32_t count, uint32_t /*sampleRate*/, bool /*isStereo*/) override {
        std::uint32_t take = count;
        if (take > capacity_ - count_) {
            take = capacity_ - count_;
            overflowFrames_ += count - take;
            assert(take == count && "the Mesen audio ring overflowed: it was not sized for this block");
        }
        if (take == 0) return;
        const std::uint32_t tail  = (head_ + count_) % capacity_;
        const std::uint32_t first = std::min(take, capacity_ - tail);
        std::memcpy(buffer_.data() + static_cast<std::size_t>(tail) * 2, samples,
                    static_cast<std::size_t>(first) * 2 * sizeof(int16_t));
        std::memcpy(buffer_.data(), samples + static_cast<std::size_t>(first) * 2,
                    static_cast<std::size_t>(take - first) * 2 * sizeof(int16_t));
        count_ += take;
    }

    void Stop() override {}
    void Pause() override {}
    void ProcessEndOfFrame() override {}
    string GetAvailableDevices() override { return {}; }
    void SetAudioDevice(string) override {}
    AudioStatistics GetStatistics() override { return {}; }

    // Frames written and not yet drained: the block clock, exactly what the vector's size / 2 meant.
    std::size_t availableFrames() const { return count_; }

    // Drain up to `frameCount` stereo frames from the head into `dest` as normalised float32. Returns the number of
    // frames actually written.
    std::uint32_t drain(float* dest, std::uint32_t frameCount) {
        const std::uint32_t take = std::min(frameCount, count_);
        if (take == 0) return 0;
        constexpr float kScale = 1.0f / 32768.0f;
        const std::uint32_t first = std::min(take, capacity_ - head_);
        for (std::uint32_t i = 0; i < first * 2; ++i) {
            dest[i] = buffer_[static_cast<std::size_t>(head_) * 2 + i] * kScale;
        }
        for (std::uint32_t i = 0; i < (take - first) * 2; ++i) {
            dest[static_cast<std::size_t>(first) * 2 + i] = buffer_[i] * kScale;
        }
        head_ = (head_ + take) % capacity_;
        count_ -= take;
        return take;
    }

    // The ring's capacity in frames (a test's window on the sizing: one block plus one Mesen flush), and how many
    // frames PlayBuffer has had to drop (0 unless a core ran a block bigger than the ring it was given).
    std::uint32_t capacityFrames() const { return capacity_; }
    std::uint64_t overflowFrames() const { return overflowFrames_; }

private:
    std::vector<int16_t> buffer_;  // capacity_ stereo frames, interleaved
    std::uint32_t        capacity_       = 0;
    std::uint32_t        head_           = 0;  // frame index of the oldest queued frame
    std::uint32_t        count_          = 0;  // frames written and not yet drained
    std::uint64_t        overflowFrames_ = 0;
};
