#include "SameBoyBackend.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <rfl/json.hpp>

#include "EmbeddedRoms.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

// Whole file into a byte vector (empty if unreadable).
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

} // namespace

SameBoyRoleConfig SameBoyBackend::decodeSameBoyRoleConfig(const std::string& json) {
    const auto r = rfl::json::read<SameBoyRoleConfig, rfl::DefaultIfMissing>(json);
    return r ? r.value() : SameBoyRoleConfig{};  // unparseable → all defaults (a no-op apply)
}

std::unique_ptr<SameBoySystem> SameBoyBackend::buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                           std::vector<std::uint8_t> romBytes,
                                                           double sampleRate) {
    auto sys = std::make_unique<SameBoySystem>(id, std::move(cfg), std::move(romBytes));
    sys->setSniffDefaultRoles(false);  // greenfield cores are bare — feature roles live in the TS kernel
    sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
    return sys;
}

std::unique_ptr<SystemBase> SameBoyBackend::build(SystemId id, const SystemBuildSpec& spec,
                                                  double sampleRate) {
    std::vector<std::uint8_t> romBytes;
    if (!spec.embeddedRom.empty()) {
        // Embedded ROM: resolve the marker to baked bytes; the format is SameBoy by fiat.
        const auto rom = rp::embeddedRom(spec.embeddedRom);
        if (rom.empty()) return nullptr;  // unknown marker
        romBytes.assign(rom.begin(), rom.end());
    } else {
        // File-backed: slurp the full ROM and sniff. SameBoy-only gate here.
        romBytes = slurpAll(spec.romPath);
        if (romBytes.empty() || detectRomFormat(romBytes) != RomFormat::SameBoy) return nullptr;
    }

    SameBoyConfig cfg;
    cfg.romPath = spec.romPath;
    cfg.model = SameBoyModel::CgbC;
    cfg.fastBoot = true;
    if (!spec.embeddedRom.empty()) {
        cfg.embeddedRom = spec.embeddedRom;
        cfg.embedRom = false;  // re-supplied from the marker on load; keeps saves small
    }
    cfg.sram = spec.sram;
    cfg.savestate = spec.savestate;
    // `spec.settings` (the opaque per-backend blob) is the seam for TS-owned SameBoy settings
    // (model / highpass / fast-boot) — not yet wired, so model/fastBoot stay backend defaults and
    // no roles are seeded. buildSameBoy activates the core bare.
    return buildSameBoy(id, std::move(cfg), std::move(romBytes), sampleRate);
}
