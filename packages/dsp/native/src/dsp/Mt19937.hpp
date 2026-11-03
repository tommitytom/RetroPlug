#pragma once

// The 32-bit Mersenne Twister, std::mt19937's engine and nothing else: the same seeding and the same sequence, which
// the standard specifies to the bit (its 10000th draw from the default seed is 4123659995, held in Dither.cpp). The
// dither kernel draws its TPDF noise from it. It is its own type rather than std::mt19937 because <random> brings
// <string>, <vector> and <iosfwd> with it (the distributions and seed_seq need them), which a device build of the
// kernel must not include; the state is the same 624 words and a position, held inline, so it allocates nothing and
// is trivially destructible.

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

class Mt19937 {
public:
    using result_type = std::uint32_t;
    static constexpr std::uint32_t default_seed = 5489u;

    constexpr Mt19937() { seed(default_seed); }
    constexpr explicit Mt19937(std::uint32_t value) { seed(value); }

    constexpr void seed(std::uint32_t value) {
        state_[0] = value;
        for (std::uint32_t i = 1; i < kN; ++i)
            state_[i] = 1812433253u * (state_[i - 1] ^ (state_[i - 1] >> 30)) + i;
        index_ = kN;
    }

    constexpr std::uint32_t operator()() {
        if (index_ >= kN) twist();
        std::uint32_t y = state_[index_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }

    static constexpr std::uint32_t min() { return 0; }
    static constexpr std::uint32_t max() { return 0xffffffffu; }

private:
    static constexpr std::uint32_t kN = 624;
    static constexpr std::uint32_t kM = 397;

    constexpr void twist() {
        for (std::uint32_t i = 0; i < kN; ++i) {
            const std::uint32_t y = (state_[i] & 0x80000000u) | (state_[(i + 1) % kN] & 0x7fffffffu);
            state_[i] = state_[(i + kM) % kN] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
        }
        index_ = 0;
    }

    std::uint32_t state_[kN] = {};
    std::size_t   index_ = kN;
};

}  // namespace rp::dsp
