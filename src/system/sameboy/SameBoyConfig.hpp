#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

#include "system/RoleConfig.hpp"
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
    // Per-system trim, dB. Smoothed at audio rate inside SameBoySystem::mixInto.
    float                     gainDb   = 0.0f;
    // Serial-link group. 0 = standalone (default). Same nonzero id on multiple
    // systems puts them in the same LinkGroup so their serial ports are
    // ferried bit-for-bit and they step in instruction-level lockstep. See
    // src/system/sameboy/LinkGroup.hpp.
    std::uint8_t              linkGroupId = 0;
    std::string               romPath;    // absolute path; populated at bootstrap or load
    Base64Bytes               romBytes;   // populated when embedRom (snapshotConfig)
    // Cartridge battery RAM (.sav contents). Path-based ROM loads slurp the
    // sibling `<rom>.sav` once and stash it here; subsequent host-project
    // saves serialize whatever the running emulator currently has, so the
    // SRAM is portable. Loaded into the emulator BEFORE `savestate`, so a
    // savestate's embedded SRAM still wins when both are set.
    Base64Bytes               sram;
    Base64Bytes               savestate;  // optional, populated when persisting

    // Roles attached to this system (LSDJ sync, MGB passthrough, etc.).
    // Round-trips through reflectcpp; SameBoySystem::onActivate replays it
    // into runtime `RomRole` instances. Empty after a fresh ROM load triggers
    // RomSniffer to fill in a default suggestion.
    std::vector<RoleConfig>   roles;
};
