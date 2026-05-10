#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// Lock-free triple-buffer of XRGB8888 frames sized at construction. One
// DSP-thread writer (the emulator's pixel-output callback) and one UI-thread
// reader.
//
// Correctness uses two atomics:
//
//   1. `seq` — a seqlock counter. Even (and nonzero) means "stable", odd
//      means "writer in progress". The reader takes a snapshot of seq
//      before and after its memcpy; if a publish() interleaved, the values
//      differ and the reader retries.
//
//   2. `readingHint` — the slot the reader most recently committed to.
//      The writer skips this slot when picking the next write slot. With
//      three slots there is always at least one slot that is neither the
//      just-published one nor the reader's; the writer picks that.
//
// Together these guarantee the reader never sees a torn frame: either the
// writer chose a different slot (so the reader's memcpy is on stable data),
// or the writer interleaved during the memcpy and the seqlock catches it.
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

    // DSP-side: pointer to the slot the emulator should write into next.
    // Stable until publish() rotates the writing slot.
    std::uint32_t* writeSlot() { return frames[writing].data(); }

    // DSP-side: announce that the current writing slot is fully written and
    // pick a fresh one. Increments `seq` to bracket the rotation so readers
    // can detect interleaving.
    void publish() {
        const std::uint64_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_release); // odd = writer in progress

        const std::uint32_t justWritten = writing;
        latest.store(justWritten, std::memory_order_release);

        // Skip the reader's slot when picking the next write slot. With
        // three slots there is always one that is neither just-published
        // nor reader-claimed.
        const std::uint32_t hint = readingHint.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < 3; ++i) {
            if (i != justWritten && i != hint) {
                writing = i;
                break;
            }
        }

        seq.store(s + 2, std::memory_order_release); // even again, new value
    }

    // UI-side: copy the most recent published frame into `dst`. Returns
    // false if no frame has been published yet, `dst` is too small, or the
    // retry budget is exhausted (extremely rare in practice).
    bool readInto(std::uint32_t* dst, std::uint32_t dstCapacityPixels) {
        if (dstCapacityPixels < static_cast<std::uint32_t>(w) * h) return false;

        constexpr int kMaxRetries = 16;
        for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
            const std::uint64_t before = seq.load(std::memory_order_acquire);
            if (before == 0) return false;          // never published
            if ((before & 1) != 0) continue;        // writer mid-publish

            const std::uint32_t idx = latest.load(std::memory_order_acquire);
            if (idx == kNone) return false;

            readingHint.store(idx, std::memory_order_release);

            std::memcpy(dst, frames[idx].data(),
                        static_cast<std::size_t>(w) * h * sizeof(std::uint32_t));

            // If a publish() ran during our memcpy, `before != after` and
            // we retry. The published-then-reader-claimed protocol means
            // subsequent publishes will skip our slot, so retries converge.
            const std::uint64_t after = seq.load(std::memory_order_acquire);
            if (before == after) return true;
        }
        return false;
    }

    static constexpr std::uint32_t kNone = UINT32_MAX;

private:
    std::uint32_t w;
    std::uint32_t h;
    std::array<std::vector<std::uint32_t>, 3> frames;
    std::atomic<std::uint64_t> seq{0};            // 0 = never published; even/odd = stable/in-progress
    std::atomic<std::uint32_t> latest{kNone};
    std::atomic<std::uint32_t> readingHint{kNone};
    std::uint32_t writing = 0;                    // DSP-thread only
};
