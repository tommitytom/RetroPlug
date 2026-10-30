#include "system/sameboy/SameBoyBackend.hpp"

#include <string>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoySystem.hpp"

std::unique_ptr<SystemBase> SameBoyBackend::build(SystemId id, const SystemBuildRequest& request,
                                                  double sampleRate, std::string* why) {
    const auto* cfg = std::get_if<SameBoyConfig>(&request.config);
    if (!cfg) {  // the factory dispatched by the config, so this is a host bug rather than a caller's
        if (why) *why = "SameBoy does not build this console";
        return nullptr;
    }
    // The ROM gate every SameBoy build passes (the embedded mGB carries the same Nintendo logo a cart does): bytes
    // that are positively another console's, or nothing at all, never reach the core.
    if (request.rom.empty() || detectRomFormat(request.rom) != RomFormat::Gb) {
        if (why) *why = "the ROM is not a Game Boy ROM";
        return nullptr;
    }
    SameBoyConfig typed = *cfg;
    // Cores are bare - feature roles (LSDj sync, MGB passthrough, ...) run as TS protocol script nodes.
    typed.sram = request.sram;
    typed.savestate = request.savestate;
    auto sys = std::make_unique<SameBoySystem>(id, std::move(typed), request.rom);
    sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
    sys->enableStateSnapshot();        // publish a tear-free savestate each block so Duplicate can clone
                                       // off the snapshot while the audio thread runs (getFrame's analogue)
    return sys;
}
