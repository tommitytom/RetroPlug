#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a GBA (Mesen2) system slot.
// Mirrors MesenNesConfig's shape so the three configs are interchangeable
// through the SystemConfig tagged union.

// Named MesenGbaConfig (not GbaConfig) to avoid colliding with Mesen's own
// `struct GbaConfig` in deps/mesen/Core/Shared/SettingTypes.h — both live at
// global scope and any TU that needs to call settings->SetGbaConfig() would
// otherwise see two definitions of the same name. The on-disk JSON tag is
// still "gba"; the rename is internal only.
struct MesenGbaConfig {
    // On-disk variant discriminator (`"kind":"gba"`).
    using Tag = rfl::Literal<"gba">;

    bool          embedRom        = true;
    bool          skipBootScreen  = true;
    // Watch `romPath` on disk; the UI thread reloads the system when the
    // file's mtime advances. No-op when romPath is empty.
    bool          reloadOnRomChange = false;
    float         gainDb          = 0.0f;
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

    // Optional path to a real GBA BIOS file. When set, MesenGbaSystem::onActivate
    // copies it into Mesen's firmware search path (`<home>/Firmware/
    // gba_bios.bin`) so FirmwareHelper::LoadGbaBootRom picks it up. When
    // empty, Mesen falls back to a zeroed boot ROM (HLE).
    std::string   biosPath;
};
