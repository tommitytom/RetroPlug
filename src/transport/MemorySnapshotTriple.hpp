#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Lock-free triple-buffer of one emulator memory region. Same seqlock +
// reader-hint discipline as FrameBufferTriple, but the slot type is a
// fixed-size std::vector<std::uint8_t> sized at construction.
//
// Writer: the DSP thread, between emulator-step boundaries (after
// onProcess() finishes a block — guaranteed internally consistent state).
// Reader: the UI thread, at uiIdle cadence. Tear-free for multi-byte
// structs because the writer copies a complete buffer into a stable slot
// before publishing.
//
// Use case: per-(system, type) memory snapshot for live UI subscriptions.
// Allocated on subscribe, freed on unsubscribe.
class MemorySnapshotTriple {
public:
    explicit MemorySnapshotTriple(std::size_t regionSize)
        : size_(regionSize) {
        for (auto& slot : slots_)
            slot.assign(regionSize, 0u);
    }

    std::size_t size() const { return size_; }

    // DSP-side: pointer to the buffer the writer should fill next. Stable
    // until publish() rotates.
    std::uint8_t* writeSlot() { return slots_[writing_].data(); }

    // DSP-side: announce the current write slot is fully written, advance.
    void publish() {
        const std::uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release); // odd = writer mid-rotate

        const std::uint32_t justWritten = writing_;
        latest_.store(justWritten, std::memory_order_release);

        const std::uint32_t hint = readingHint_.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < 3; ++i) {
            if (i != justWritten && i != hint) {
                writing_ = i;
                break;
            }
        }

        seq_.store(s + 2, std::memory_order_release); // even = stable again
    }

    // UI-side: copy the latest published snapshot into `dst`. Returns false
    // if nothing has been published yet, dst is too small, or the retry
    // budget is exhausted (extremely rare in practice).
    bool readInto(std::vector<std::uint8_t>& dst) {
        if (dst.size() < size_) dst.resize(size_);
        return readInto(dst.data(), dst.size());
    }

    bool readInto(std::uint8_t* dst, std::size_t dstCapacity) {
        if (dst == nullptr || dstCapacity < size_) return false;

        constexpr int kMaxRetries = 16;
        for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
            const std::uint64_t before = seq_.load(std::memory_order_acquire);
            if (before == 0) return false;          // never published
            if ((before & 1) != 0) continue;        // writer in progress

            const std::uint32_t idx = latest_.load(std::memory_order_acquire);
            if (idx == kNone) return false;

            readingHint_.store(idx, std::memory_order_release);

            std::memcpy(dst, slots_[idx].data(), size_);

            const std::uint64_t after = seq_.load(std::memory_order_acquire);
            if (before == after) return true;
        }
        return false;
    }

    static constexpr std::uint32_t kNone = UINT32_MAX;

private:
    std::size_t                                size_;
    std::array<std::vector<std::uint8_t>, 3>   slots_;
    std::atomic<std::uint64_t>                 seq_{0};         // 0 = never published; even/odd = stable/in-progress
    std::atomic<std::uint32_t>                 latest_{kNone};
    std::atomic<std::uint32_t>                 readingHint_{kNone};
    std::uint32_t                              writing_ = 0;    // DSP-thread only
};
