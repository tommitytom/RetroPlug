#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "SystemFactory.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

class SameBoySystem;

// Builds a real SameBoySystem (Game Boy). The only place SameBoyConfig is constructed on the
// build path: resolves the ROM (embedded marker or file + sniff), decodes the opaque settings
// blob (today: the LSDJ sync-role seed), seeds SRAM/savestate, and onActivates. SameBoy-only —
// a non-GB file-backed ROM is rejected (nullptr).
class SameBoyBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;

    // The shared ctor + seed-order + onActivate sequence, reused by reloadSystem (which derives
    // its own SameBoyConfig from an existing core). SRAM/savestate go into cfg BEFORE construct —
    // a live core restores them inside onActivate. cfg + romBytes are consumed.
    static std::unique_ptr<SameBoySystem> buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                       std::vector<std::uint8_t> romBytes,
                                                       double sampleRate);
};
