#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Per-ROM-build LSDJ memory layout. Right now only the bundled stock
// (v9.4.2) and Arduinoboy (v9.3.3) builds are supported; both share an
// identical kit-bank layout (banks 8..23 for kit slots 0..15).
//
// Full ROM-version-aware port of the legacy `old/src/lsdj/OffsetLookup.h`
// is deferred until support for user-supplied LSDJ ROMs lands. When that
// happens this file is the natural home for the version table — the
// public surface `kitBankForSlot()` will pick up an additional `RomInfo`
// parameter and dispatch internally.

namespace rp::lsdj::OffsetLookup {

inline constexpr std::size_t kSlotCount = 16;

// Slot → cart-bank index, stable across the two bundled LSDJ builds.
// Each bank is 16 KB so the slot's byte offset in `system.rom_` is
// `kitBankForSlot(slot) * 0x4000`.
inline constexpr std::array<std::uint8_t, kSlotCount> kSlotBank = {
    8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};

constexpr std::uint8_t kitBankForSlot(std::uint8_t slot) {
    return kSlotBank[slot];
}

} // namespace rp::lsdj::OffsetLookup
