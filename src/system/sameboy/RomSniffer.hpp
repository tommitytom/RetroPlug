#pragma once

#include <cstdint>
#include <vector>

// Best-effort classification of a Game Boy ROM by reading its cartridge header.
// Used at SameBoySystem::onActivate time to pick a default RomRole when the
// stored config has none — the user's explicit role list always wins.
//
// LSDJ joins this enum at step 08; Mesen-side ROMs use a separate sniffer.
enum class RomKind : std::uint8_t {
    Generic = 0,
    Mgb     = 1,
};

// Reads the 15-byte title field at 0x0134 (cartridge header) and matches it
// against known signatures. Anything shorter than the header range is Generic.
// Heuristic only — different ROMs sometimes share the same title prefix; the
// caller treats this as a default suggestion, not a contract.
RomKind detectRomKind(const std::vector<std::uint8_t>& rom);
