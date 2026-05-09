#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    GameboyModel              model    = GameboyModel::CgbC;
    bool                      fastBoot = true;
    std::string               romPath;    // absolute path; populated at bootstrap or load
    std::vector<std::uint8_t> savestate;  // optional, populated when persisting

    // Roles attached to this system (LSDJ sync, MGB passthrough, etc.).
    // The variant has no inhabitants in step 1; the field exists so the type is in place.
    // std::vector<RoleConfig>   roles;
};
