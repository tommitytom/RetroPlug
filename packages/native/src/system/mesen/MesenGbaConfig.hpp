#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a GBA (Mesen2) system slot.
// Mirrors MesenNesConfig's shape so the three concrete configs are handled
// uniformly by the system backends.

// Named MesenGbaConfig (not GbaConfig) to avoid colliding with Mesen's own
// `struct GbaConfig` in deps/mesen/Core/Shared/SettingTypes.h — both live at
// global scope and any TU that needs to call settings->SetGbaConfig() would
// otherwise see two definitions of the same name. The on-disk JSON tag is
// still "gba"; the rename is internal only.
struct MesenGbaConfig {
    // On-disk variant discriminator (`"kind":"gba"`).
    using Tag = rfl::Literal<"gba">;

    bool          skipBootScreen  = true;
    float         gainDb          = 0.0f;
    // The file name Mesen stages the ROM under and logs it by. A node's build has only bytes and leaves this empty (the
    // core stages "rom.gba"); a direct construction in a test may name a file.
    std::string   romPath;
    // The battery and savestate seeds a node's resources carry, as a build takes them: the emulator owns both once it
    // is booted, and a savestate's embedded SRAM wins when both are set.
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;

    // Optional path to a real GBA BIOS file. When set, MesenGbaSystem::onActivate
    // copies it into Mesen's firmware search path (`<home>/Firmware/
    // gba_bios.bin`) so FirmwareHelper::LoadGbaBootRom picks it up. When
    // empty, Mesen falls back to a zeroed boot ROM (HLE).
    std::string   biosPath;
};
