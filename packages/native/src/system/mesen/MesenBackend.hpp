#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "system/SystemFactory.hpp"

class SystemBase;

// The TS-owned "mesen" system-role config as it crosses the wire (JSON): the NES emulator knobs the UI
// edits. Field names match the TS role schema (coreRoles.ts). Decoded both live (applyRoleConfig) and
// at construct (the settings blob). Defaults mirror MesenNesConfig, so a missing field is a no-op.
// (The role attaches to any Mesen system; GBA ignores these NES-only fields.)
struct MesenNesRoleConfig {
    std::uint32_t region            = 0;      // ConsoleRegion (Auto/Ntsc/Pal/Dendy/NtscJapan)
    bool          removeSpriteLimit = false;
    std::uint32_t channelExportMode = 0;      // 0 = Mix, 1 = pins, 3 = 5 mono (CLI-only; spec/10 §5/§5b)
    double        apuLatencyMs      = 1.4;    // APU flush window as latency (ms); live. ~1.4ms ≈ 2500 cyc NTSC
    // Cartridge-accuracy switches: 0 = the documented chip, 1 = match an Everdrive N8 Pro's FPGA core.
    // s5bNoise 1 = no 5B noise generator (so tone-AND-noise mutes the channel); mmc5PhaseReset 1 = a
    // $5003/$5007 write does not restart the pulse duty sequencer. Both live, both measured on hardware.
    std::uint32_t s5bNoise          = 0;
    std::uint32_t mmc5PhaseReset    = 0;
};

// The SMS/GG slice of that same TS "mesen" role. A separate struct rather than more fields on the one
// above: reflect-cpp decodes each tolerantly (DefaultIfMissing), so one JSON blob can feed both, and a
// per-platform struct keeps NES knobs from reading as if they meant something on a Master System.
struct MesenSmsRoleConfig {
    // Route the YM2413 (FM) at all. Load-bearing rather than cosmetic: Mesen models $F2 as a MUX, and
    // its PSG branch memsets the buffer, so a tracker that writes $F2 = $01 at boot is SILENT on the
    // PSG until this is off. Which side to default to is still an open question - this is the knob that
    // lets it be answered by ear.
    bool enableFm = true;
};

// The Mesen core — one backend spanning multiple platforms. Dispatches on `spec.platform` to build the
// right Mesen system (NES, GBA, or Master System / Game Gear): slurp the ROM, gate on the platform's
// magic, seed SRAM/savestate, and onActivate (which boots the core, or fails gracefully on a bad ROM).
// GBA runs on Mesen's HLE boot ROM (biosPath left empty). The opaque settings blob carries the "mesen"
// role knobs - region / remove-sprite-limit for NES, enableFm for SMS/GG; GBA has none yet. An unknown
// platform or a mismatched ROM → nullptr.
class MesenBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;

    // Parse the TS "mesen" role-config JSON (forward-tolerant: an absent field takes its default).
    // The single place that JSON is decoded — shared by live applyRoleConfig + the NES construct blob.
    static MesenNesRoleConfig decodeMesenNesRoleConfig(const std::string& json);

    // The SMS/GG twin of the above, over the same JSON blob.
    static MesenSmsRoleConfig decodeMesenSmsRoleConfig(const std::string& json);
};
