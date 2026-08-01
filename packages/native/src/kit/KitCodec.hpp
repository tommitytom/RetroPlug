#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kit/SampleCache.hpp"

// Console-neutral kit-codec interface. A `KitCodec` is the ONLY console-
// specific part of kit compilation: given decoded source audio it produces
// the packed per-sample byte stream and assembles the console's kit bank.
// Everything expensive and shared — file decode + dedupe (SampleCache),
// per-sample threading, and the r8brain resampler + effect chain — lives in
// the generic `KitCompiler` (KitCompiler.hpp) and is reused by every codec.
//
// Implementations:
//   - rp::lsdj::LsdjKitCodec — Game Boy 4-bit inverted/rotated nibble PCM,
//     16 KB bank (SampleUtil / KitUtil).
//   - rp::risa::… (planned) — NES DPCM (1-bit delta), 8 KB bank.
//
// A codec is constructed with the whole compile request (kit name + per-slot
// source paths + per-slot options) and answers per-slot queries. `encode` is
// called concurrently across slots on the compiler's thread pool, so it MUST
// be const and touch only the passed `SampleData` plus the codec's own
// immutable per-slot config.

namespace rp::kit {

// Where slot i's audio comes from. Just the path today (the cache keys on it);
// windowing/effects are the codec's business inside `encode`.
struct SampleSource {
    std::string path;
};

struct CompiledKit {
    bool                      ok = false;
    std::string               error;
    std::vector<std::uint8_t> bytes;    // the assembled kit bank on success
    std::uint64_t             hash = 0; // FNV-64 of `bytes` (dirty tracking)
};

class KitCodec {
public:
    virtual ~KitCodec() = default;

    // Number of sample slots this compile fills.
    virtual std::size_t sampleCount() const = 0;

    // Source audio file for slot i (decoded + cached by the compiler).
    virtual SampleSource source(std::size_t i) const = 0;

    // Resample + effects + pack slot i's decoded audio into the bank's sample
    // encoding. Called off-thread, concurrently across slots — must be pure /
    // thread-safe. An empty return leaves the slot unused. `decoded` is the
    // mono float32 source at its native rate.
    virtual std::vector<std::uint8_t> encode(std::size_t i,
                                             const SampleData& decoded) const = 0;

    // Assemble the console kit bank from the per-slot encoded byte streams
    // (in slot order; an empty entry means "no sample here").
    virtual std::vector<std::uint8_t> assemble(
        const std::vector<std::vector<std::uint8_t>>& encoded) const = 0;
};

} // namespace rp::kit
