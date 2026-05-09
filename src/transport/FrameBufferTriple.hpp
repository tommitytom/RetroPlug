#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// Lock-free triple-buffer of XRGB8888 frames sized at construction. One DSP-thread
// writer (the emulator's pixel-output callback) and one UI-thread reader. The DSP
// writes into `frames[writing]`, calls publish() to atomically swap that slot into
// `latest`, then advances `writing` to a slot that is neither the just-published
// one nor the slot the reader most recently committed to. With three slots and
// the published-index discipline below, the writer never picks the slot the
// reader is currently copying out of.
class FrameBufferTriple {
public:
    FrameBufferTriple(std::uint32_t width, std::uint32_t height)
        : w(width), h(height) {
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        for (auto& slot : frames)
            slot.assign(pixels, 0u);
    }

    std::uint32_t width()  const { return w; }
    std::uint32_t height() const { return h; }

    // DSP-side: pointer into the slot the emulator should write into next.
    // Stable until the next publish() rotates the writing slot.
    std::uint32_t* writeSlot() { return frames[writing].data(); }

    // DSP-side: announce the just-finished frame; pick a fresh writing slot.
    void publish() {
        const std::uint32_t justWritten = writing;
        latest.store(justWritten, std::memory_order_release);
        // Pick the slot that's neither the latest nor the one the reader
        // last touched. With 3 slots there is always exactly one such slot.
        for (std::uint32_t i = 0; i < 3; ++i) {
            if (i != justWritten && i != readingHint.load(std::memory_order_acquire)) {
                writing = i;
                return;
            }
        }
        // Fallback (shouldn't happen given the invariant, but stays safe):
        writing = (justWritten + 1) % 3;
    }

    // UI-side: copy the most recent published frame into `dst`. Returns false if
    // no frame has been published yet.
    bool readInto(std::uint32_t* dst, std::uint32_t dstCapacityPixels) {
        const std::uint32_t idx = latest.load(std::memory_order_acquire);
        if (idx == kNone) return false;
        if (dstCapacityPixels < static_cast<std::uint32_t>(w) * h) return false;
        readingHint.store(idx, std::memory_order_release);
        std::memcpy(dst, frames[idx].data(),
                    static_cast<std::size_t>(w) * h * sizeof(std::uint32_t));
        return true;
    }

    static constexpr std::uint32_t kNone = UINT32_MAX;

private:
    std::uint32_t w;
    std::uint32_t h;
    std::array<std::vector<std::uint32_t>, 3> frames;
    std::atomic<std::uint32_t> latest{kNone};
    std::atomic<std::uint32_t> readingHint{kNone};
    std::uint32_t writing = 0;  // DSP-thread only
};
