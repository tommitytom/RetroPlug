#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

#include "util/Base64Bytes.hpp"

// Plain-data, reflectcpp-friendly config for a SameBoy system slot.
// Lives in the DSP-owned ProjectConfig tree; mirrored to the UI cache.

enum class GameboyModel : std::uint32_t {
    Auto = 0,
    DmgB = 1,
    CgbC = 2,
    CgbE = 3,
    Agb  = 4,
};

struct SameBoyConfig {
    // On-disk variant discriminator (`"kind":"sameboy"`). Locked spelling.
    using Tag = rfl::Literal<"sameboy">;

    GameboyModel              model    = GameboyModel::CgbC;
    bool                      fastBoot = true;
    // When true (default), saves embed `romBytes` so projects survive ROM
    // file moves. When false, only `romPath` is persisted and the ROM is
    // re-read from disk on load.
    bool                      embedRom = true;
    std::string               romPath;    // absolute path; populated at bootstrap or load
    Base64Bytes               romBytes;   // populated when embedRom (snapshotConfig)
    Base64Bytes               savestate;  // optional, populated when persisting

    // Roles attached to this system (LSDJ sync, MGB passthrough, etc.).
    // The variant has no inhabitants in step 1; the field exists so the type is in place.
    // std::vector<RoleConfig>   roles;
};
