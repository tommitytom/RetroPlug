#pragma once

#include <atomic>
#include <cstddef>

// Power-of-two bounded SPSC ring. Single producer, single consumer; lock-free on
// both sides; `tryPush` returns false when full so the producer can drop or
// coalesce as it sees fit. No allocation on either thread once constructed — the
// slots are inline. `T` must be trivially copyable (the slot assignment is a
// plain byte copy); heap ownership crosses as raw owning pointers inside `T`.
//
// This is the shared ring extracted from CommandQueue/EventQueue; those are now
// `using` aliases over it (UI→DSP commands, DSP→UI events), and other subsystems
// instantiate it with their own POD payload rather than inventing a new scheme.
template <class T, std::size_t Capacity>
class SpscRing {
public:
    static constexpr std::size_t kCapacity = Capacity;
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "kCapacity must be a power of two");

    SpscRing() = default;
    SpscRing(const SpscRing&)            = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    bool tryPush(const T& v) {
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & (kCapacity - 1);
        if (next == readIdx.load(std::memory_order_acquire))
            return false; // full
        slots[w] = v;
        writeIdx.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out) {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        if (r == writeIdx.load(std::memory_order_acquire))
            return false; // empty
        out = slots[r];
        readIdx.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> writeIdx{0};
    alignas(64) std::atomic<std::size_t> readIdx{0};
    T slots[kCapacity]{};
};
