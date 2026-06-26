#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "FixedArray.hpp"
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
    Byte                                              activeProjectIndex = 0xFF; // 0xFF = none
    FixedArray<Byte, 30>                              reserved{};                // 0x8120, opaque
    Song                                              workingSong;
    // Stored projects are heap-allocated (each embeds a full Song): inline
    // storage for 32 would put ~2 MB on the stack and blow MSVC's 1 MB default.
    // shared_ptr (not unique_ptr) so the copy-based FixedArray reflector still
    // compiles; serializes identically to optional (null-or-object).
    FixedArray<std::shared_ptr<StoredProject>, 32>    projects{};
};

} // namespace rp::lsdj::model
