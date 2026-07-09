#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "SystemFactory.hpp"

class SystemBase;

// The TS-owned "mesen" system-role config as it crosses the wire (JSON): the NES emulator knobs the UI
// edits. Field names match the TS role schema (coreRoles.ts). Decoded both live (applyRoleConfig) and
// at construct (the settings blob). Defaults mirror MesenNesConfig, so a missing field is a no-op.
// (The role attaches to any Mesen system; GBA ignores these NES-only fields.)
struct MesenNesRoleConfig {
    std::uint32_t region            = 0;      // ConsoleRegion (Auto/Ntsc/Pal/Dendy/NtscJapan)
    bool          removeSpriteLimit = false;
};

// The Mesen core — one backend spanning multiple platforms. Dispatches on `spec.platform` to build the
// right Mesen system (NES or GBA): slurp the ROM, gate on the platform's magic, seed SRAM/savestate,
// and onActivate (which boots the core, or fails gracefully on a bad ROM). GBA runs on Mesen's HLE
// boot ROM (biosPath left empty). For NES the opaque settings blob carries the "mesen" role knobs
// (region / remove-sprite-limit); GBA has none yet. An unknown platform or a mismatched ROM → nullptr.
class MesenBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;

    // Parse the TS "mesen" role-config JSON (forward-tolerant: an absent field takes its default).
    // The single place that JSON is decoded — shared by live applyRoleConfig + the NES construct blob.
    static MesenNesRoleConfig decodeMesenNesRoleConfig(const std::string& json);
};
