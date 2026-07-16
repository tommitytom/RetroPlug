#pragma once

#include <cstddef>
#include <cstdint>

namespace rp::hash {

// FNV-1a 64-bit. Not cryptographic — used for cheap dedup / dirty tracking
// where collisions are tolerable. ~1 cycle/byte on modern CPUs.
inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

} // namespace rp::hash
