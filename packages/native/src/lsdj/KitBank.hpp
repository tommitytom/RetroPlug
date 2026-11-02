#pragma once

#include <cstddef>
#include <cstdint>

// The LSDj kit bank's numbers: a 16 KB bank whose data region is up to 0x3fa0 bytes of packed 4-bit nibbles (two
// samples a byte, stored inverted), resampled to kGameboySampleRate. The TypeScript kit compiler packs one
// (packages/retroplug/src/kit/pack.ts), and native reads or writes none: the tests name these numbers, Dither.test.cpp
// for the kit rate and OfflineJobs.test.cpp for the seam's float-ratio cap, which is measured in kit frames.

namespace rp::lsdj {

inline constexpr std::uint32_t kGameboySampleRate = 11468;

inline constexpr std::size_t kMaxSampleBytes  = 0x3fa0;                 // packed nibbles, 2 samples a byte
inline constexpr std::size_t kMaxSampleFrames = kMaxSampleBytes * 2;    // 32576 frames

} // namespace rp::lsdj
