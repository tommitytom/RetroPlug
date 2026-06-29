#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

#include "system/RoleConfig.hpp"

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
    std::string   romPath;
    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
    std::uint32_t savSuffix = 0;
    // Binary blobs live in the .rplg zip as raw entries — see ProjectBinaries.
    std::vector<std::uint8_t> romBytes;
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;

    // Roles attached to this system (currently NesN8MidiRole). Empty triggers
    // the NES sniffer to fill in a default.
    std::vector<RoleConfig> roles;
};
