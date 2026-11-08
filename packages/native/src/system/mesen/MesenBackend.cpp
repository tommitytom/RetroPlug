#include "system/mesen/MesenBackend.hpp"

#include <string>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"

namespace rp::mesen {

namespace {

// Shared Mesen boot tail: activate the core, reject the build if Mesen's LoadRom failed (a corrupt ROM that passed the
// magic gate) rather than adopting a dead system, then opt into the live snapshot plane so the control plane's
// readState/readSram see fresh bytes instead of the boot seed frozen forever. `T` is MesenNesSystem, MesenGbaSystem or
// MesenSmsSystem.
template <typename T>
std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate, std::uint32_t maxBlockFrames,
                                      std::string* why) {
    // Before onActivate: a node hands over the plan's largest block, so the core sizes its ring and its per-block
    // scratches to it as it activates instead of taking the default and being grown before its first block. A build
    // outside a plan leaves it 0, and the core keeps the console's default.
    if (maxBlockFrames != 0) sys->reserveAudio(maxBlockFrames);
    sys->onActivate(sampleRate);
    if (!sys->activated()) {  // core rejected the ROM at LoadRom → fail the construct
        if (why) *why = "Mesen refused the ROM";
        return nullptr;
    }
    sys->enableStateSnapshot();             // republish a tear-free savestate every ~0.5s (Duplicate/Save State)
    return sys;                             // implicit upcast to unique_ptr<SystemBase>
}

std::unique_ptr<SystemBase> buildNesSystem(SystemId id, MesenNesConfig cfg, std::vector<std::uint8_t> romBytes,
                                           double sampleRate, std::uint32_t maxBlockFrames, std::string* why) {
    // The platform's RomFormat gate confirms the bytes actually match the requested platform (a mislabelled ROM is
    // rejected before Mesen sees it). The node type that made the config is what picked this console.
    if (detectRomFormat(romBytes) != RomFormat::Nes) {
        if (why) *why = "the ROM is not a NES ROM";
        return nullptr;
    }
    return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
                     maxBlockFrames, why);
}

std::unique_ptr<SystemBase> buildGbaSystem(SystemId id, MesenGbaConfig cfg, std::vector<std::uint8_t> romBytes,
                                           double sampleRate, std::uint32_t maxBlockFrames, std::string* why) {
    if (detectRomFormat(romBytes) != RomFormat::Gba) {
        if (why) *why = "the ROM is not a GBA ROM";
        return nullptr;
    }
    // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).
    return bootMesen(std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
                     maxBlockFrames, why);
}

std::unique_ptr<SystemBase> buildSmsSystem(SystemId id, MesenSmsConfig cfg, std::vector<std::uint8_t> romBytes,
                                           double sampleRate, std::uint32_t maxBlockFrames, std::string* why) {
    // Deliberately NOT an exact-match gate like the two above. The Sega magic is not required by
    // any boot ROM, so headerless homebrew is legitimate and TS admits it on the strength of the
    // file extension (platform.ts). An == RomFormat::Sms gate here would accept such a ROM in the
    // UI and then fail the construct with no diagnostic. What this still catches - and all it needs
    // to - is bytes that are positively SOMETHING ELSE, which is the mislabelled-ROM case the gate
    // exists for. Content wins whenever it says anything, exactly as it does in TS.
    const RomFormat fmt = detectRomFormat(romBytes);
    if (fmt != RomFormat::Sms && fmt != RomFormat::Unknown) {
        if (why) *why = "the ROM is not a Master System or Game Gear ROM";
        return nullptr;
    }
    return bootMesen(std::make_unique<MesenSmsSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
                     maxBlockFrames, why);
}

}  // namespace

std::unique_ptr<SystemBase> buildNesCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                         std::string* why) {
    const auto* cfg = static_cast<const MesenNesConfig*>(request.config);
    if (!cfg) {
        if (why) *why = "the NES build was given no NES config";
        return nullptr;
    }
    MesenNesConfig typed = *cfg;
    typed.sram = std::move(request.sram);
    typed.savestate = std::move(request.savestate);
    return buildNesSystem(id, std::move(typed), std::move(request.rom), sampleRate, request.maxBlockFrames, why);
}

std::unique_ptr<SystemBase> buildGbaCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                         std::string* why) {
    const auto* cfg = static_cast<const MesenGbaConfig*>(request.config);
    if (!cfg) {
        if (why) *why = "the GBA build was given no GBA config";
        return nullptr;
    }
    MesenGbaConfig typed = *cfg;
    typed.sram = std::move(request.sram);
    typed.savestate = std::move(request.savestate);
    return buildGbaSystem(id, std::move(typed), std::move(request.rom), sampleRate, request.maxBlockFrames, why);
}

std::unique_ptr<SystemBase> buildSmsCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                         std::string* why) {
    const auto* cfg = static_cast<const MesenSmsConfig*>(request.config);
    if (!cfg) {
        if (why) *why = "the Master System build was given no Master System config";
        return nullptr;
    }
    MesenSmsConfig typed = *cfg;
    typed.sram = std::move(request.sram);
    typed.savestate = std::move(request.savestate);
    typed.romName = std::move(request.romName);
    return buildSmsSystem(id, std::move(typed), std::move(request.rom), sampleRate, request.maxBlockFrames, why);
}

}  // namespace rp::mesen
