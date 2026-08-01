#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a SameBoy system slot. Built by the
// SameBoy backend and held as the running system's config_; the TS layer owns
// project persistence and mirrors this to the UI cache.

enum class SameBoyModel : std::uint32_t {
    Auto = 0,
    DmgB,    // Game Boy             (GB_MODEL_DMG_B)
    Mgb,     // Game Boy Pocket      (GB_MODEL_MGB)
    Sgb,     // Super Game Boy NTSC  (GB_MODEL_SGB_NTSC_NO_SFC)
    SgbPal,  // Super Game Boy PAL   (GB_MODEL_SGB_PAL_NO_SFC)
    Sgb2,    // Super Game Boy 2     (GB_MODEL_SGB2_NO_SFC)
    Cgb0,    // Game Boy Color CPU-0 (GB_MODEL_CGB_0)
    CgbA,    // Game Boy Color CPU-A (GB_MODEL_CGB_A)
    CgbB,    // Game Boy Color CPU-B (GB_MODEL_CGB_B)
    CgbC,    // Game Boy Color CPU-C (GB_MODEL_CGB_C)
    CgbD,    // Game Boy Color CPU-D (GB_MODEL_CGB_D)
    CgbE,    // Game Boy Color CPU-E (GB_MODEL_CGB_E)
    Agb,     // Game Boy Advance     (GB_MODEL_AGB)
    Gbp,     // Game Boy Player      (GB_MODEL_GBP)
};

// Audio highpass filter mode. Mirrors SameBoy's GB_highpass_mode_t.
// `Accurate` models the real GB's analog HPF (~120 Hz); `RemoveDcOffset` is
// a stronger HPF that softens release transients at the cost of authenticity.
enum class SameBoyHighpass : std::uint32_t {
    Off            = 0,
    Accurate       = 1,
    RemoveDcOffset = 2,
};

// CGB colour correction. Mirrors SameBoy's GB_color_correction_mode_t ordinal-for-ordinal, so the
// cast in applyDisplayConfig is direct. Only affects a CGB-family core (it acts on the 15-bit ->
// RGB conversion); a DMG/MGB core ignores it. `Disabled` is the historical RetroPlug setting.
enum class SameBoyColorCorrection : std::uint32_t {
    Disabled            = 0,
    CorrectCurves       = 1,
    ModernBalanced      = 2,  // SameBoy's own default (was EMULATE_HARDWARE)
    ModernBoostContrast = 3,  // (was PRESERVE_BRIGHTNESS)
    ReduceContrast      = 4,
    LowContrast         = 5,
    ModernAccurate      = 6,
};

// DMG palette. NOT a SameBoy enum — the core takes a GB_palette_t pointer, so this indexes the four
// built-ins it exports (GB_PALETTE_GREY / _DMG / _MGB / _GBL). Only affects DMG rendering. `Grey` is
// what the core falls back to when no palette is set, i.e. RetroPlug's historical appearance.
enum class SameBoyDmgPalette : std::uint32_t {
    Grey = 0,
    Dmg  = 1,
    Mgb  = 2,
    Gbl  = 3,
};

struct SameBoyConfig {
    // On-disk variant discriminator (`"kind":"sameboy"`).
    using Tag = rfl::Literal<"sameboy">;

    SameBoyModel              model    = SameBoyModel::CgbC;
    SameBoyHighpass           highpass = SameBoyHighpass::Accurate;
    bool                      fastBoot = true;
    // --- display knobs (TS "sameboy" role; see coreRoles.ts) ---------------
    // Every default below reproduces what SameBoySystem hardcoded before these were configurable, so
    // an old project renders identically. All are live — none needs a core restart.
    SameBoyColorCorrection    colorCorrection = SameBoyColorCorrection::Disabled;
    SameBoyDmgPalette         dmgPalette      = SameBoyDmgPalette::Grey;
    double                    lightTemperature = 0.0;  // -1 cool .. +1 warm, CGB only
    // When true, the UI thread watches `romPath` and triggers a reload
    // (preserving current SRAM, dropping savestate) when the file changes.
    // No-op when `romPath` is empty (embed-only project).
    bool                      reloadOnRomChange = false;
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
    // Loose-battery disambiguator. 0 => this system owns the plain sibling
    // `<rom>.sav`; N>=2 => `<rom>-N.sav`. Assigned when a ROM ends up loaded
    // into more than one system (Duplicate Instance, or adding the same file
    // twice) so the instances keep independent battery files instead of all
    // auto-saving over `<rom>.sav`. Persisted so it survives reload.
    std::uint32_t             savSuffix = 0;
    // Explicit battery-file override. Empty => derive `<rom>.sav` / `<rom>-N.sav`
    // from romPath + savSuffix (the default). Non-empty => this exact file, set
    // when the user pairs a hand-picked `.sav` with the ROM in the load browser;
    // all battery reads/writes (auto-save, Save SRAM, project-load restore) then
    // target it instead of the sibling.
    std::string               savPath;
    // Non-empty => this system's ROM is baked into the binary (id, e.g. "mgb"),
    // not a file on disk. romPath stays empty (so no .sav / ROM-watcher); the
    // bytes are supplied by rp::embeddedRom() on load (see EmbeddedRoms.hpp).
    // Survives the thin-JSON strip (not a binary blob), so a saved project
    // re-supplies the embedded ROM on reopen.
    std::string               embeddedRom;
    // Binary blobs live in the .rplg zip as raw entries (see ProjectBinaries).
    // In project.json they always serialize as `[]` because ProjectSerialization
    // strips them before the JSON pass.
    std::vector<std::uint8_t> romBytes;   // populated when embedRom
    // Cartridge battery RAM (.sav contents). Path-based ROM loads slurp the
    // sibling `<rom>.sav` once and stash it here; subsequent host-project
    // saves serialize whatever the running emulator currently has, so the
    // SRAM is portable. Loaded into the emulator BEFORE `savestate`, so a
    // savestate's embedded SRAM still wins when both are set.
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;  // optional, populated when persisting
};
