#include "system/sameboy/SameBoyBackend.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <rfl/json.hpp>

#include "EmbeddedRoms.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "util/SlurpFile.hpp"

namespace {

// Parse the TS "sameboy" role-config JSON (forward-tolerant: an absent field takes its default). The single place
// that JSON is decoded: the construct blob.
SameBoyRoleConfig decodeSameBoyRoleConfig(const std::string& json) {
    const auto r = rfl::json::read<SameBoyRoleConfig, rfl::DefaultIfMissing>(json);
    return r ? r.value() : SameBoyRoleConfig{};  // unparseable → all defaults (a no-op apply)
}

} // namespace

std::unique_ptr<SameBoySystem> SameBoyBackend::buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                           std::vector<std::uint8_t> romBytes,
                                                           double sampleRate, std::string* why) {
    // The ROM gate every SameBoy build passes (the embedded mGB carries the same Nintendo logo a cart does): bytes
    // that are positively another console's, or nothing at all, never reach the core.
    if (romBytes.empty() || detectRomFormat(romBytes) != RomFormat::Gb) {
        if (why) *why = "the ROM is not a Game Boy ROM";
        return nullptr;
    }
    // Cores are bare - feature roles (LSDj sync, MGB passthrough, ...) run as TS protocol script nodes.
    auto sys = std::make_unique<SameBoySystem>(id, std::move(cfg), std::move(romBytes));
    sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
    sys->enableStateSnapshot();        // publish a tear-free savestate each block so Duplicate can clone
                                       // off the snapshot while the audio thread runs (getFrame's analogue)
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
    } else if (!spec.romBytes.empty()) {
        // TS-supplied effective ROM (e.g. LSDj asset overrides applied non-destructively): use it instead
        // of reading romPath, but still sniff it. cfg.romPath below keeps the on-disk path for the watcher.
        romBytes = spec.romBytes;
    } else {
        // File-backed: slurp the full ROM and sniff. SameBoy-only gate here.
        romBytes = slurpAll(spec.romPath);
    }

    SameBoyConfig cfg;
    cfg.romPath = spec.romPath;
    if (!spec.embeddedRom.empty()) {
        cfg.embeddedRom = spec.embeddedRom;
        cfg.embedRom = false;  // re-supplied from the marker on load; keeps saves small
    }
    cfg.sram = spec.sram;
    cfg.savestate = spec.savestate;
    // TS-owned SameBoy settings: the "sameboy" role config crosses in the opaque `settings` blob so a
    // loaded non-default model/highpass is applied AT construct (no post-build restart that would nuke
    // the just-restored savestate). A fresh build sends no blob → these stay the role-schema defaults
    // (model CgbC / highpass Accurate / fastBoot true), which are the SameBoyConfig struct defaults.
    const std::string settings(spec.settings.begin(), spec.settings.end());
    const SameBoyRoleConfig role =
        settings.empty() ? SameBoyRoleConfig{} : decodeSameBoyRoleConfig(settings);
    cfg.model = static_cast<SameBoyModel>(role.model);
    cfg.highpass = static_cast<SameBoyHighpass>(role.highpass);
    cfg.linkGroupId = static_cast<std::uint8_t>(role.linkGroupId);
    cfg.fastBoot = role.fastBoot;
    cfg.colorCorrection = static_cast<SameBoyColorCorrection>(role.colorCorrection);
    cfg.dmgPalette = static_cast<SameBoyDmgPalette>(role.dmgPalette);
    cfg.lightTemperature = role.lightTemperature;
    return buildSameBoy(id, std::move(cfg), std::move(romBytes), sampleRate, nullptr);
}

std::unique_ptr<SystemBase> SameBoyBackend::buildTyped(SystemId id, const SystemBuildRequest& request,
                                                       double sampleRate, std::string* why) {
    const auto* cfg = std::get_if<SameBoyConfig>(&request.config);
    if (!cfg) {  // the factory dispatched by the config, so this is a host bug rather than a caller's
        if (why) *why = "SameBoy does not build this console";
        return nullptr;
    }
    SameBoyConfig typed = *cfg;
    typed.sram = request.sram;
    typed.savestate = request.savestate;
    return buildSameBoy(id, std::move(typed), request.rom, sampleRate, why);
}
