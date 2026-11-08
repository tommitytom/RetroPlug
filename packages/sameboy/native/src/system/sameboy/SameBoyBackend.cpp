#include "system/sameboy/SameBoyBackend.hpp"

#include <string>

#include "system/sameboy/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace rp::sameboy {

std::unique_ptr<SystemBase> buildCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                      std::string* why) {
    // The node that carries this function is the one that made the config, so a request without one is a broken build
    // rather than a document's mistake.
    const auto* cfg = static_cast<const SameBoyConfig*>(request.config);
    if (!cfg) {
        if (why) *why = "the Game Boy build was given no Game Boy config";
        return nullptr;
    }
    // The ROM gate every SameBoy build passes (the embedded mGB carries the same Nintendo logo a cart does): bytes
    // that are positively another console's, or nothing at all, never reach the core.
    if (!isGameBoyRom(request.rom)) {
        if (why) *why = "the ROM is not a Game Boy ROM";
        return nullptr;
    }
    SameBoyConfig typed = *cfg;
    // Cores are bare - feature roles (LSDj sync, MGB passthrough, ...) run as TS protocol script nodes.
    typed.sram = std::move(request.sram);
    typed.savestate = std::move(request.savestate);
    auto sys = std::make_unique<SameBoySystem>(id, std::move(typed), std::move(request.rom));
    // Before onActivate: a node hands over the plan's largest block, so the per-block scratches are sized to it as the
    // core activates. A build outside a plan leaves it 0, and the core keeps the default block.
    if (request.maxBlockFrames != 0) sys->reserveAudio(request.maxBlockFrames);
    sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
    sys->enableStateSnapshot();        // publish a tear-free savestate each block so Duplicate can clone
                                       // off the snapshot while the audio thread runs (getFrame's analogue)
    return sys;
}

}  // namespace rp::sameboy
