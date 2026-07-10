#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a Mesen-backed NES system slot.
// Mirrors SameBoyConfig's shape so the two are interchangeable through the
// SystemConfig tagged union.

struct MesenNesConfig {
    // On-disk variant discriminator (`"kind":"nes"`).
    using Tag = rfl::Literal<"nes">;

    bool          embedRom = true;
    // Watch `romPath` on disk; the UI thread reloads the system when the
    // file's mtime advances. No-op when romPath is empty.
    bool          reloadOnRomChange = false;
    float         gainDb   = 0.0f;
    // TS-owned "mesen" system-role knobs (coreRoles.ts). region = ConsoleRegion
    // (0=Auto,1=Ntsc,2=Pal,3=Dendy,4=NtscJapan) — applied at construct, live edit
    // needs a reset. removeSpriteLimit toggles the PPU 8-sprites/line cap (live).
    std::uint32_t region            = 0;
    bool          removeSpriteLimit = false;
    std::string   romPath;
    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
    std::uint32_t savSuffix = 0;
    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
    // a user-paired `.sav` file that all battery I/O targets.
    std::string   savPath;
    // Binary blobs live in the .rplg zip as raw entries — see ProjectBinaries.
    std::vector<std::uint8_t> romBytes;
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;
};
