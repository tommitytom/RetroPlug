#pragma once

#include <array>
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
    // std::array rather than `T slots[kCapacity]`, and it is load-bearing for more than taste. As a raw
    // array GCC 13 mis-attributes the base object of `slots[w]`: the index comes from a relaxed atomic
    // load, which the optimiser treats as opaque, so it cannot prove w < kCapacity, and it then anchors
    // the access to the member the array happens to sit behind - `readIdx._M_i`, 8 bytes at offset 64,
    // with slots starting at 72. Every push is then "writing 17 bytes into a region of size 8"
    // (-Wstringop-overflow), which is nonsense: writeIdx is private, starts at 0 and is only ever stored
    // pre-masked, so the write is always in bounds.
    //
    // Masking at the use site does not help (GCC keeps the wrong base object and just reports a bounded
    // offset range instead), nor does asserting the bound with __builtin_unreachable. std::array does,
    // at -O2 and -O3, because the subscript goes through its own object. clang never warned either way.
    // Layout, size, alignment and trivial-copyability are unchanged, and the emitted code differs only
    // in register allocation - verified by comparing both against the same TU.
    std::array<T, kCapacity> slots{};
};
