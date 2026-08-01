#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <string>

#include "system/SystemFactory.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

class SameBoySystem;

// The TS-owned "sameboy" system-role config as it crosses the wire (JSON): the emulator knobs the UI
// edits. Field names match the TS role schema (coreRoles.ts). Decoded both live (applyRoleConfig)
// and at construct (the settings blob). Defaults mirror the struct + TS defaults, so a missing field
// is a no-op.
struct SameBoyRoleConfig {
    std::uint32_t model       = static_cast<std::uint32_t>(SameBoyModel::CgbC);
    std::uint32_t highpass    = static_cast<std::uint32_t>(SameBoyHighpass::Accurate);
    std::uint32_t linkGroupId = 0;
    bool          fastBoot    = true;
    // Display knobs. Defaults reproduce the pre-configurable hardcoded behaviour, so a project saved
    // before these existed decodes (DefaultIfMissing) to exactly what it used to render.
    std::uint32_t colorCorrection  = static_cast<std::uint32_t>(SameBoyColorCorrection::Disabled);
    std::uint32_t dmgPalette       = static_cast<std::uint32_t>(SameBoyDmgPalette::Grey);
    double        lightTemperature = 0.0;
};

// Builds a real SameBoySystem (Game Boy). The only place SameBoyConfig is constructed on the
// build path: resolves the ROM (embedded marker or file + sniff), decodes the opaque settings
// blob (today: the LSDJ sync-role seed), seeds SRAM/savestate, and onActivates. SameBoy-only —
// a non-GB file-backed ROM is rejected (nullptr).
class SameBoyBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;

    // The shared ctor + seed-order + onActivate + enableStateSnapshot sequence. SRAM/savestate go
    // into cfg BEFORE construct — a live core restores them inside onActivate. cfg + romBytes are
    // consumed. (Reload/duplicate are TS orchestration over constructSystem now, not a native reuse.)
    static std::unique_ptr<SameBoySystem> buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                       std::vector<std::uint8_t> romBytes,
                                                       double sampleRate);

    // Parse the TS "sameboy" role-config JSON (forward-tolerant: an absent field takes its default).
    // The single place that JSON is decoded — shared by live applyRoleConfig + the construct blob.
    static SameBoyRoleConfig decodeSameBoyRoleConfig(const std::string& json);
};
