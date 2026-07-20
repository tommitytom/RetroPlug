#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// LSDJ kit sample encoding helpers. Ported from old/src/lsdj/SampleUtil.h.
//
// LSDJ stores per-kit samples as packed 4-bit nibbles (two samples per byte).
// Two quirks of the GB DAC and LSDJ's compensation for them are baked into
// the encoding and MUST be preserved bit-for-bit if patched kits are to
// sound identical to ones built by the upstream tool:
//
//  1. Nibble values are stored INVERTED: stored = 0xF - amplitude. Decoded
//     amplitude is therefore `0xF - stored`.
//  2. Starting with LSDJ 9.2.0 each 32-sample frame is ROTATED RIGHTWARD by
//     one position to skip the first sample, compensating for a hardware
//     wave-refresh bug. Sample i of the source ends up at position (i+1)%32
//     in the encoded stream.
//
// Audio is processed in 32-sample frames (= 16 packed bytes). The bank
// layout calls them "wave frames" and they align with the GB's wave RAM.
//
// We use std::vector<float> / std::vector<uint8_t> instead of the legacy
// orb::Float32Buffer / orb::Uint8Buffer; the orb framework is being stripped
// (see porting/README.md).

namespace rp::lsdj::SampleUtil {

inline constexpr std::size_t SAMPLES_PER_BYTE_4BIT = 2;
inline constexpr std::size_t SAMPLES_PER_FRAME    = 32;
inline constexpr std::size_t BYTES_PER_FRAME      = 16;

// Decode packed nibbles -> float32 in [-1, 1], NO rotation undo. Used when
// the source stream isn't a LSDJ kit (e.g. raw 4-bit PCM dumps) and wave-
// refresh compensation never applied in the first place.
inline void convertNibblesToF32(const std::vector<std::uint8_t>& input,
                                std::vector<float>& output) {
    output.resize(input.size() * SAMPLES_PER_BYTE_4BIT);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const std::uint8_t byte = input[i];
        output[i * 2]     = ((0xF - (byte >> 4))  / 15.0f) * 2.0f - 1.0f;
        output[i * 2 + 1] = ((0xF - (byte & 0xF)) / 15.0f) * 2.0f - 1.0f;
    }
}

// Decode packed nibbles -> float32 in [-1, 1], undoing the LSDJ 9.2.0+
// rightward rotation. Position i in the encoded stream holds what was
// source-sample (i-1); undo by reading from position (i+1)%32.
inline void convertNibblesToF32WithRotation(const std::vector<std::uint8_t>& input,
                                            std::vector<float>& output) {
    const std::size_t numChunks = input.size() / BYTES_PER_FRAME;
    output.resize(numChunks * SAMPLES_PER_FRAME);

    for (std::size_t chunk = 0; chunk < numChunks; ++chunk) {
        float samples[SAMPLES_PER_FRAME];

        for (std::size_t i = 0; i < BYTES_PER_FRAME; ++i) {
            const std::uint8_t byte = input[chunk * BYTES_PER_FRAME + i];
            samples[i * 2]     = static_cast<float>(0xF - (byte >> 4));
            samples[i * 2 + 1] = static_cast<float>(0xF - (byte & 0xF));
        }

        for (std::size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            const std::size_t rotatedPos = (i + 1) % SAMPLES_PER_FRAME;
            output[chunk * SAMPLES_PER_FRAME + i] =
                (samples[rotatedPos] / 15.0f) * 2.0f - 1.0f;
        }
    }
}

// Encode float32 input pre-scaled to [0, 15] (caller responsible for clamp +
// scale + round) into packed nibbles WITH rotation. Sample i goes to
// position (i+1) % 32 inside its frame; values are inverted (stored = 0xF -
// amp). Both quirks are needed for byte-exact compatibility with kits built
// by the LSDJ project's own kit tool.
// `rotate` applies the LSDJ 9.2.0+ rightward frame rotation; pass false for a pre-9.2.0 target ROM (whose
// playback engine expects un-rotated frames), so import is byte-correct for the actual LSDj version.
inline void convertScaledF32ToNibbles(const std::vector<float>& input,
                                      std::vector<std::uint8_t>& output,
                                      bool rotate = true) {
    const std::size_t numChunks = input.size() / SAMPLES_PER_FRAME;
    output.resize(numChunks * BYTES_PER_FRAME);

    const float* src = input.data();
    std::uint8_t* dst = output.data();

    for (std::size_t chunk = 0; chunk < numChunks; ++chunk) {
        std::uint8_t samples[SAMPLES_PER_FRAME];

        if (rotate) {
            // Rotation: sample i goes to position (i+1)%32. Position 0 gets the
            // last sample wrapped around, so the encoded frame is "rotated
            // rightward by one" compared to the source.
            samples[0] = 0xF - static_cast<std::uint8_t>(src[SAMPLES_PER_FRAME - 1]);
            for (std::size_t i = 1; i < SAMPLES_PER_FRAME; ++i) {
                samples[i] = 0xF - static_cast<std::uint8_t>(src[i - 1]);
            }
        } else {
            // Un-rotated (pre-9.2.0): sample i stays at position i.
            for (std::size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                samples[i] = 0xF - static_cast<std::uint8_t>(src[i]);
            }
        }

        for (std::size_t i = 0; i < BYTES_PER_FRAME; ++i) {
            *dst++ = static_cast<std::uint8_t>(
                (samples[i * 2] << 4) | samples[i * 2 + 1]);
        }
        src += SAMPLES_PER_FRAME;
    }
}

// Encode float32 input in [-1, 1] (arbitrary range — clamped) into packed
// nibbles. Combines clamp + scale + invert + rotation in one pass.
inline void convertF32ToNibbles(const std::vector<float>& input,
                                std::vector<std::uint8_t>& output) {
    const std::size_t numChunks = input.size() / SAMPLES_PER_FRAME;
    output.resize(numChunks * BYTES_PER_FRAME);

    for (std::size_t chunk = 0; chunk < numChunks; ++chunk) {
        std::uint8_t samples[SAMPLES_PER_FRAME];

        for (std::size_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float f = input[chunk * SAMPLES_PER_FRAME + i];
            f = (std::clamp(f, -1.0f, 1.0f) + 1.0f) * 0.5f;          // [0, 1]
            const std::uint8_t nibble = static_cast<std::uint8_t>(
                std::round(f * 15.0f));                              // [0, 15]
            samples[(i + 1) % SAMPLES_PER_FRAME] = 0xF - nibble;     // invert + rotate
        }

        for (std::size_t i = 0; i < BYTES_PER_FRAME; ++i) {
            output[chunk * BYTES_PER_FRAME + i] = static_cast<std::uint8_t>(
                (samples[i * 2] << 4) | samples[i * 2 + 1]);
        }
    }
}

} // namespace rp::lsdj::SampleUtil
