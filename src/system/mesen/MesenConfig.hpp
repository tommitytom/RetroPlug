#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

#include "system/RoleConfig.hpp"
#include "util/Base64Bytes.hpp"

// Plain-data, reflectcpp-friendly config for a Mesen system slot.
// Mirrors SameBoyConfig's shape so the two are interchangeable through the
// SystemConfig tagged union. Console kind is fixed to NES for now (the legacy
// project never shipped anything else); future SNES/PCE/etc. would either
// gain alternative configs or a `consoleType` enum on this struct.

struct MesenConfig {
    // On-disk variant discriminator (`"kind":"mesen"`). Locked spelling.
    using Tag = rfl::Literal<"mesen">;

    bool          embedRom = true;
    // Watch `romPath` on disk; the UI thread reloads the system when the
    // file's mtime advances. No-op when romPath is empty.
    bool          reloadOnRomChange = false;
    float         gainDb   = 0.0f;
    std::string   romPath;
    Base64Bytes   romBytes;
    Base64Bytes   sram;
    Base64Bytes   savestate;

    // Roles attached to this system. Mesen-side roles will land in step 17C
    // (NesN8MidiRole). Empty triggers the NES sniffer to fill in a default.
    std::vector<RoleConfig> roles;
};
