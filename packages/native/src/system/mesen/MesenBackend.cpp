#include "system/mesen/MesenBackend.hpp"

#include <string>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"

namespace {

// Shared Mesen boot tail: activate the core, reject the build if Mesen's LoadRom failed (a corrupt ROM that passed the
// magic gate) rather than adopting a dead system, then opt into the live snapshot plane so the control plane's
// readState/readSram see fresh bytes instead of the boot seed frozen forever. `T` is MesenNesSystem, MesenGbaSystem or
// MesenSmsSystem.
template <typename T>
std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate, std::string* why) {
    sys->onActivate(sampleRate);
    if (!sys->activated()) {  // core rejected the ROM at LoadRom → fail the construct
        if (why) *why = "Mesen refused the ROM";
        return nullptr;
    }
    sys->enableStateSnapshot();             // republish a tear-free savestate every ~0.5s (Duplicate/Save State)
    return sys;                             // implicit upcast to unique_ptr<SystemBase>
}

} // namespace

std::unique_ptr<SystemBase> MesenBackend::buildNes(SystemId id, MesenNesConfig cfg,
                                                   std::vector<std::uint8_t> romBytes, double sampleRate,
                                                   std::string* why) {
    // The platform's RomFormat gate confirms the bytes actually match the requested platform (a mislabelled ROM is
    // rejected before Mesen sees it). The node type that made the config is what picked this console.
    if (detectRomFormat(romBytes) != RomFormat::Nes) {
        if (why) *why = "the ROM is not a NES ROM";
        return nullptr;
    }
    return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
}

std::unique_ptr<SystemBase> MesenBackend::buildGba(SystemId id, MesenGbaConfig cfg,
                                                   std::vector<std::uint8_t> romBytes, double sampleRate,
                                                   std::string* why) {
    if (detectRomFormat(romBytes) != RomFormat::Gba) {
        if (why) *why = "the ROM is not a GBA ROM";
        return nullptr;
    }
    // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).
    return bootMesen(std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
}

std::unique_ptr<SystemBase> MesenBackend::buildSms(SystemId id, MesenSmsConfig cfg,
                                                   std::vector<std::uint8_t> romBytes, double sampleRate,
                                                   std::string* why) {
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
    return bootMesen(std::make_unique<MesenSmsSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
}

std::unique_ptr<SystemBase> MesenBackend::build(SystemId id, const SystemBuildRequest& request,
                                                double sampleRate, std::string* why) {
    if (const auto* nes = std::get_if<MesenNesConfig>(&request.config)) {
        MesenNesConfig cfg = *nes;
        cfg.sram = request.sram;
        cfg.savestate = request.savestate;
        return buildNes(id, std::move(cfg), request.rom, sampleRate, why);
    }
    if (const auto* gba = std::get_if<MesenGbaConfig>(&request.config)) {
        MesenGbaConfig cfg = *gba;
        cfg.sram = request.sram;
        cfg.savestate = request.savestate;
        return buildGba(id, std::move(cfg), request.rom, sampleRate, why);
    }
    if (const auto* sms = std::get_if<MesenSmsConfig>(&request.config)) {
        MesenSmsConfig cfg = *sms;
        cfg.sram = request.sram;
        cfg.savestate = request.savestate;
        cfg.romName = request.romName;
        return buildSms(id, std::move(cfg), request.rom, sampleRate, why);
    }
    // The factory dispatched by the config, so this is a host bug rather than a caller's.
    if (why) *why = "Mesen does not build this console";
    return nullptr;
}
