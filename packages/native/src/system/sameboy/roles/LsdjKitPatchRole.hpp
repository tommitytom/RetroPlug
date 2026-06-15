#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rfl/Literal.hpp"

#include "lsdj/Effects.hpp"
#include "system/RomRole.hpp"

namespace rp::lsdj {

// Per-sample metadata that's part of project state. This is the source-side
// info needed to fully *re-compile* the sample — so a project save doesn't have
// to carry the baked sample bytes. `path` + `offset`/`length` + `effects`
// reproduce the exact compiled output (see ProjectKitRecompile + KitCompiler).
struct LsdjSampleConfig {
    std::string   path;                  // source audio file
    std::string   name;                  // 3-char uppercase
    std::uint8_t  pitch    = 0x7F;       // 0x7F = neutral; honored by future revisions
    std::uint8_t  volume   = 0xFF;       // 0xFF = full; ditto
    std::uint64_t sourceHash = 0;        // FNV-64 of source bytes (UI dirty tracking)
    std::size_t   offset = 0;            // skip the first N source frames
    std::size_t   length = 0;            // 0 = use everything from offset
    std::vector<LsdjEffect> effects;     // gain / filter / dither, applied at compile
};

// One kit slot's persisted state. `compiledBytes` is the emulator-side truth —
// what the running LSDJ ROM sees in the bank for `slot` — and is what the zip
// export bundles so it loads without the source WAVs. The path-only JSON save
// drops it (and `compiledHash`); the kit is recompiled from `samples` on load.
struct LsdjKitConfig {
    std::uint8_t  slot = 0;                          // 0..15
    std::string   name;                              // 6-char kit name (matches LSDJ UI)
    std::vector<std::uint8_t> compiledBytes;         // 16 KB; empty == "no kit"
    std::uint64_t compiledHash = 0;                  // FNV-64 of compiledBytes
    std::vector<LsdjSampleConfig> samples;
};

// Role config. Discriminator `"kind":"lsdj-kit-patch"`. Attached
// automatically by the sniffer next to `LsdjSyncConfig` when an LSDJ
// ROM is loaded — both roles are orthogonal.
struct LsdjKitPatchConfig {
    using Tag = rfl::Literal<"lsdj-kit-patch">;

    std::vector<LsdjKitConfig> kits;
};

} // namespace rp::lsdj

// Runtime side of the role. Holds a queue of pending kit patches and
// applies them at the top of each audio block. Patches are written to
// both `SameBoySystem::rom_` (so project snapshots round-trip) AND the
// running emulator's ROM via SameBoy's `GB_get_direct_access`.

class LsdjKitPatchRole final : public RomRole {
public:
    // Matches rp::lsdj::OffsetLookup::kSlotCount. Hardcoded constant
    // rather than a cross-include because the public role surface is
    // referenced from RPC DTOs that shouldn't pull the offset table in.
    static constexpr std::size_t kSlotCount = 16;

    LsdjKitPatchRole();
    ~LsdjKitPatchRole() override;

    void onAttach(SameBoySystem& system) override;
    void onProcessBlock(SameBoySystem& system, const AudioBlockInfo& info) override;
    std::string_view kind() const override { return "lsdj-kit-patch"; }

    // Queue a kit patch. Called by the DSP command-drain when a
    // `PatchKitCommand` arrives from the UI. `kitBytes` must be exactly
    // `Kit::kSize` bytes; smaller/larger inputs are dropped with a stderr
    // warning. Thread-safety: caller must invoke from the DSP thread.
    void queuePatch(std::uint8_t kitIndex, std::vector<std::uint8_t> kitBytes);

    // Queue every non-empty kit from `config_` for application. Called
    // from `onAttach` (project load) and on config replacement.
    void queueAllFromConfig(const rp::lsdj::LsdjKitPatchConfig& config);

private:
    std::array<std::optional<std::vector<std::uint8_t>>, kSlotCount> pending_{};
};
