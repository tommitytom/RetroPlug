#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lsdj/Effects.hpp"

// LSDJ kit compilation. Ported from old/src/lsdj/KitUtil.{h,cpp} and
// old/src/lsdj/Rom.h (the Kit struct's binary layout).
//
// Kit bank layout (16 KB == 0x4000 bytes):
//   0x0000  offset table (16 × uint16 little-endian, addressing relative to
//           the bank-base 0x4000 — so offsets[0] is always 0x4060)
//   0x0022  sample names: 15 × 3 bytes (uppercase, '-' padded; first byte
//           of an empty slot is 0x00 to mark "no sample here")
//   0x0052  kit name: 6 bytes (space-padded)
//   0x0060  sample data, up to 0x3fa0 bytes of packed 4-bit nibbles

namespace rp::lsdj {

inline constexpr std::uint32_t kGameboySampleRate = 11468;

struct Kit {
    static constexpr std::size_t kSize             = 0x4000; // bank size
    static constexpr std::size_t kMaxSamples       = 15;
    static constexpr std::size_t kMaxSampleSpace   = 0x3fa0;
    static constexpr std::size_t kMaxSampleFrames  = kMaxSampleSpace * 2; // 4-bit, 2 per byte
    static constexpr std::size_t kSampleNameOffset = 0x22;
    static constexpr std::size_t kSampleNameSize   = 3;
    static constexpr std::size_t kNameOffset       = 0x52;
    static constexpr std::size_t kNameSize         = 6;
    static constexpr std::size_t kSampleDataOffset = 0x60;     // kSize - kMaxSampleSpace
    static constexpr std::uint16_t kBankBase       = 0x4000;   // offsets stored absolute to bank base
};

namespace KitUtil {

// Resample one sample buffer from `inputSampleRate` to `outputSampleRate`
// using r8brain's CDSPResampler24. Output length is rounded down from
// `buffer.size() * (outputSampleRate / inputSampleRate)`.
void convertSamplerate(double inputSampleRate,
                       double outputSampleRate,
                       const std::vector<float>& buffer,
                       std::vector<float>& target);

// A single sample's input to the kit compiler.
struct SampleInput {
    std::string             name;      // up to 3 chars; uppercased + '-' padded
    std::vector<float>      data;      // mono float [-1, 1] at `sampleRate`
    std::uint32_t           sampleRate = 44100;
    std::size_t             offset     = 0;   // skip the first N frames
    std::size_t             length     = 0;   // 0 = use everything from offset
    std::vector<LsdjEffect> effects;          // gain, filter, dither
};

// Resample + apply effects + 4-bit nibble-pack one sample. Output is the
// raw byte stream destined for the kit bank's sample-data region (no
// offset-table or naming concerns). Effect ordering matches legacy:
// non-dither effects run first at the *input* sample rate; dither runs
// after resampling, on the scaled-to-[0,15] domain. Manual scaling +
// rounding is performed in the no-dither branch so the path always feeds
// nibble-packing the same way.
//
// Returns the compressed bytes. Caller decides how to slot them into a
// kit (KitUtil::buildKit below).
std::vector<std::uint8_t> compileSample(const SampleInput& sample);

// Assemble a 16 KB kit bank from a kit name + per-sample compiled bytes.
// `samples` is { name, compiled-bytes } in slot order; up to 15 entries
// honored, anything past that is dropped. Compiled bytes are clipped to
// fit the remaining sample-data space.
std::vector<std::uint8_t> buildKit(std::string_view kitName,
                                   const std::vector<std::pair<std::string,
                                                               std::vector<std::uint8_t>>>& samples);

} // namespace KitUtil
} // namespace rp::lsdj
