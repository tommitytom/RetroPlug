#include "system/mesen/MesenBackend.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <rfl/json.hpp>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"

namespace {

// Whole file into a byte vector (empty if unreadable).
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// Shared Mesen boot tail (mirrors SameBoyBackend::buildSameBoy): activate the core, reject the build
// if Mesen's LoadRom failed (a corrupt ROM that passed the magic gate) rather than adopting a dead
// system, then opt into the live snapshot plane so the control plane's readState/readSram see fresh
// bytes instead of the boot seed frozen forever. `T` is MesenNesSystem, MesenGbaSystem or
// MesenSmsSystem.
template <typename T>
std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate) {
    sys->onActivate(sampleRate);
    if (!sys->activated()) return nullptr;  // core rejected the ROM at LoadRom → fail the construct
    sys->enableStateSnapshot();             // republish a tear-free savestate every ~0.5s (Duplicate/Save State)
    return sys;                             // implicit upcast to unique_ptr<SystemBase>
}

} // namespace

MesenNesRoleConfig MesenBackend::decodeMesenNesRoleConfig(const std::string& json) {
    const auto r = rfl::json::read<MesenNesRoleConfig, rfl::DefaultIfMissing>(json);
    return r ? r.value() : MesenNesRoleConfig{};  // unparseable → all defaults (a no-op apply)
}

MesenSmsRoleConfig MesenBackend::decodeMesenSmsRoleConfig(const std::string& json) {
    const auto r = rfl::json::read<MesenSmsRoleConfig, rfl::DefaultIfMissing>(json);
    return r ? r.value() : MesenSmsRoleConfig{};  // unparseable → all defaults (a no-op apply)
}

std::unique_ptr<SystemBase> MesenBackend::build(SystemId id, const SystemBuildSpec& spec,
                                                double sampleRate) {
    // TS-supplied effective ROM (e.g. risa asset overrides applied non-destructively) takes precedence over
    // reading romPath — mirroring SameBoyBackend. cfg.romPath below keeps the on-disk path for the watcher +
    // .sav resolution; only the loaded bytes differ. The per-platform RomFormat gate below still sniffs it.
    std::vector<std::uint8_t> romBytes = spec.romBytes.empty() ? slurpAll(spec.romPath) : spec.romBytes;
    if (romBytes.empty()) return nullptr;

    // Dispatch on the platform: one Mesen core, two systems. The RomFormat gate confirms the bytes
    // actually match the requested platform (a mislabelled ROM is rejected).
    if (spec.platform == "nes") {
        if (detectRomFormat(romBytes) != RomFormat::Nes) return nullptr;
        MesenNesConfig cfg;
        cfg.romPath = spec.romPath;
        cfg.sram = spec.sram;
        cfg.savestate = spec.savestate;
        // TS-owned "mesen" role knobs cross in the opaque settings blob so a loaded non-default region
        // is applied AT construct (configureNes runs before LoadRom → no post-build reset). A fresh
        // build sends no blob → the MesenNesConfig defaults (Auto region / sprite limit on).
        const std::string settings(spec.settings.begin(), spec.settings.end());
        const MesenNesRoleConfig role =
            settings.empty() ? MesenNesRoleConfig{} : decodeMesenNesRoleConfig(settings);
        cfg.region = role.region;
        cfg.removeSpriteLimit = role.removeSpriteLimit;
        cfg.channelExportMode = role.channelExportMode;
        cfg.apuLatencyMs = role.apuLatencyMs;
        return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes)),
                         sampleRate);
    }

    if (spec.platform == "gba") {
        if (detectRomFormat(romBytes) != RomFormat::Gba) return nullptr;
        MesenGbaConfig cfg;
        cfg.romPath = spec.romPath;
        cfg.sram = spec.sram;
        cfg.savestate = spec.savestate;
        // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).
        return bootMesen(std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes)),
                         sampleRate);
    }

    // Master System / Game Gear - ONE system class, the platform string picks the machine.
    if (spec.platform == "sms" || spec.platform == "gg") {
        // Deliberately NOT an exact-match gate like the two above. The Sega magic is not required by
        // any boot ROM, so headerless homebrew is legitimate and TS admits it on the strength of the
        // file extension (platform.ts). An == RomFormat::Sms gate here would accept such a ROM in the
        // UI and then fail the construct with no diagnostic. What this still catches - and all it needs
        // to - is bytes that are positively SOMETHING ELSE, which is the mislabelled-ROM case the gate
        // exists for. Content wins whenever it says anything, exactly as it does in TS.
        const RomFormat fmt = detectRomFormat(romBytes);
        if (fmt != RomFormat::Sms && fmt != RomFormat::Unknown) return nullptr;
        MesenSmsConfig cfg;
        cfg.romPath = spec.romPath;
        cfg.sram = spec.sram;
        cfg.savestate = spec.savestate;
        // The machine, which is the whole difference between the two platforms here. It reaches Mesen
        // as the staged file's EXTENSION (stageRom), because SmsConsole::LoadRom picks its model from
        // that and nothing else.
        cfg.gameGear = (spec.platform == "gg");
        // configureSms runs before LoadRom, so a loaded non-default FM setting has to be applied AT
        // construct - same reason the NES branch above passes region through the settings blob.
        const std::string settings(spec.settings.begin(), spec.settings.end());
        cfg.enableFm = settings.empty() ? MesenSmsRoleConfig{}.enableFm
                                        : decodeMesenSmsRoleConfig(settings).enableFm;
        return bootMesen(std::make_unique<MesenSmsSystem>(id, std::move(cfg), std::move(romBytes)),
                         sampleRate);
    }

    return nullptr;  // Mesen doesn't serve this platform
}
