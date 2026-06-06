#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <rfl/Result.hpp>

#include "lsdj/model/Song.hpp"

// Binary codec for the 0x8000-byte LSDj song body. v1 targets the modern
// (fmt22) layout; decode reads the semantic model out of the packed bytes,
// encode writes it back (the exact inverse of decode).
namespace rp::lsdj::codec {

// Decode a 0x8000-byte song body (e.g. a sav's working-memory song) into the
// model. The format version is read from offset 0x7FFF. Returns an error if the
// buffer is too small.
rfl::Result<model::Song> decodeSong(std::span<const std::uint8_t> songBytes);

// Encode the model into a 0x8000-byte song body. `templateBytes`, when 0x8000
// bytes, is used as the starting buffer (regions the model doesn't own —
// reserved gaps, work/total-time clocks, unallocated entity slots — pass
// through verbatim); pass the original bytes for a round-trip, or a default
// template when authoring. Allocation tables are regenerated from the model's
// `optional` presence.
std::vector<std::uint8_t> encodeSong(const model::Song& song,
                                     std::span<const std::uint8_t> templateBytes = {});

} // namespace rp::lsdj::codec
