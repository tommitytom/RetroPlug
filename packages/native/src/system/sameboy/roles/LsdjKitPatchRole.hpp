#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rfl/Literal.hpp"

#include "system/RomRole.hpp"

namespace rp::lsdj {

// Per-sample metadata that's part of project state. The compiled bytes
// live on `LsdjKitConfig` below; this struct carries the source-side
// info needed to *re-compile* the sample from the UI without a fresh
// drag-drop. Effects/dither are referenced by name via the LsdjEffect
// variant rather than embedded here — kept lean so the project JSON
// stays small.
struct LsdjSampleConfig {
    std::string   path;                  // source audio file
    std::string   name;                  // 3-char uppercase
    std::uint8_t  pitch    = 0x7F;       // 0x7F = neutral; honored by future revisions
    std::uint8_t  volume   = 0xFF;       // 0xFF = full; ditto
    std::uint64_t sourceHash = 0;        // FNV-64 of source bytes (UI dirty tracking)
};

// One kit slot's persisted state. The 16 KB `compiledBytes` is the
// emulator-side truth — what the running LSDJ ROM sees in the bank for
// `slot`. `compiledHash` matches `compiledBytes` and is what the UI
// compares against to decide whether a re-patch is needed. The bytes live
// in the .rplg zip as a raw entry (see ProjectBinaries); the field
// serializes as `[]` in project.json.
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
