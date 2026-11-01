#pragma once

#include <cstddef>
#include <cstdint>

namespace rp::hash {

inline constexpr std::uint64_t kFnv1a64Basis = 0xcbf29ce484222325ULL;

// FNV-1a 64-bit. Not cryptographic — used for cheap dedup / dirty tracking
// where collisions are tolerable. ~1 cycle/byte on modern CPUs. Passing the
// hash of what came before as `h` continues it: hashing a stream in pieces
// gives the hash of the whole.
inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size,
                             std::uint64_t h = kFnv1a64Basis) {
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

} // namespace rp::hash
