#pragma once

#include <cstdint>
#include <span>

#include <rfl/Result.hpp>

#include "lsdj/model/Song.hpp"

// Binary codec for the 0x8000-byte LSDj song body. v1 targets the modern
// (fmt22) layout; decode reads the semantic model out of the packed bytes.
namespace rp::lsdj::codec {

// Decode a 0x8000-byte song body (e.g. a sav's working-memory song) into the
// model. The format version is read from offset 0x7FFF. Returns an error if the
// buffer is too small.
rfl::Result<model::Song> decodeSong(std::span<const std::uint8_t> songBytes);

} // namespace rp::lsdj::codec
