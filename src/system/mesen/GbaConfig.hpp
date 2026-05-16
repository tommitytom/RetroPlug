#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

#include "system/RoleConfig.hpp"
#include "util/Base64Bytes.hpp"

// Plain-data, reflectcpp-friendly config for a GBA (Mesen2) system slot.
// Mirrors MesenConfig's shape so the three configs are interchangeable
// through the SystemConfig tagged union.

// Named GbaSystemConfig (not GbaConfig) to avoid colliding with Mesen's own
// `struct GbaConfig` in deps/mesen/Core/Shared/SettingTypes.h — both live at
// global scope and any TU that needs to call settings->SetGbaConfig() would
// otherwise see two definitions of the same name. The on-disk JSON tag is
// still "gba"; the rename is internal only.
struct GbaSystemConfig {
    // On-disk variant discriminator (`"kind":"gba"`). Locked spelling.
    using Tag = rfl::Literal<"gba">;

    bool          embedRom        = true;
    bool          skipBootScreen  = true;
    float         gainDb          = 0.0f;
    std::string   romPath;
    Base64Bytes   romBytes;
    Base64Bytes   sram;
    Base64Bytes   savestate;

    // Optional path to a real GBA BIOS file. When set, GbaSystem::onActivate
    // copies it into Mesen's firmware search path (`<home>/Firmware/
    // gba_bios.bin`) so FirmwareHelper::LoadGbaBootRom picks it up. When
    // empty, Mesen falls back to a zeroed boot ROM (HLE).
    std::string   biosPath;

    // Reserved for future Mesen-side GBA roles.
    std::vector<RoleConfig> roles;
};
