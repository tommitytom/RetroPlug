#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "Song.hpp"
#include "Types.hpp"

// The full 128 KiB LSDj .sav image: a working-memory song plus up to 32 stored
// projects (each a named, versioned song, RLE-compressed on disk).
namespace rp::lsdj::model {

struct StoredProject {
    std::string name;       // up to 8 chars
    Byte        version = 0;
    Song        song;
};

struct Sav {
    Byte                                          activeProjectIndex = 0xFF; // 0xFF = none
    std::array<Byte, 30>                          reserved{};                // 0x8120, opaque
    Song                                          workingSong;
    std::array<std::optional<StoredProject>, 32>  projects{};
};

} // namespace rp::lsdj::model
