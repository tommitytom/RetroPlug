#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <rfl/Result.hpp>

#include "lsdj/model/Sav.hpp"

// Codec for the full 128 KiB LSDj .sav image: working-memory song (raw, at file
// offset 0) + 512-byte header at 0x8000 (project names/versions, 'jk' magic,
// active-project index, 191-entry block allocation table, 30-byte reserved
// region) + the RLE-compressed stored-project archive.
namespace rp::lsdj::codec {

inline constexpr std::size_t kSavSize = 0x20000; // 128 KiB

// Decode a 128 KiB sav image. Errors if too small or the 'jk' magic is absent.
rfl::Result<model::Sav> decodeSav(std::span<const std::uint8_t> savBytes);

// Encode the model to a 128 KiB image. `templateBytes`, when 0x20000 bytes,
// seeds the buffer so regions the model doesn't own pass through (pass the
// original for a round-trip; omit for authoring).
std::vector<std::uint8_t> encodeSav(const model::Sav& sav,
                                    std::span<const std::uint8_t> templateBytes = {});

} // namespace rp::lsdj::codec
