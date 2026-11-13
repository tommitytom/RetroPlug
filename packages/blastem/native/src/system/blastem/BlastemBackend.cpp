#include "system/blastem/BlastemBackend.hpp"

#include <utility>

#include "system/SystemBase.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {
namespace {

void refuse(std::string* why, const char* reason) {
    if (why) *why = reason;
}

}  // namespace

std::unique_ptr<SystemBase> buildMdCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                        std::string* why) {
    if (!request.config) {
        refuse(why, "no Mega Drive config in the build request");
        return nullptr;
    }
    // The gate first, as every console's build does: BlastEm takes any bytes it is handed and boots noise off them,
    // and the same four bytes are what the shell's classifier reads (BlastemRom.hpp), so a ROM the shell accepted is
    // never refused here for a reason the shell could not have seen.
    if (!isMdRom(request.rom)) {
        refuse(why, "not a Mega Drive ROM: no \"SEGA\" header at $100 (or a 32X image, which this core does not run)");
        return nullptr;
    }
    BlastemMdConfig cfg = *static_cast<const BlastemMdConfig*>(request.config);
    cfg.romName = request.romName;
    cfg.sram = std::move(request.sram);
    // The battery and the state are seeds the boot consumes, so they move into the config rather than being copied a
    // second time (SystemBuild.hpp's rule for the request's bytes).
    cfg.savestate = std::move(request.savestate);
    auto sys = std::make_unique<BlastemMdSystem>(id, std::move(cfg), std::move(request.rom));
    // Before onActivate: a node hands over the plan's largest block, so the core sizes its rings and its per-block
    // scratches to it as it activates rather than taking the default and being grown before its first block. A build
    // outside a plan leaves it 0, and the core keeps the console's default.
    if (request.maxBlockFrames != 0) sys->reserveAudio(request.maxBlockFrames);
    sys->onActivate(sampleRate);
    // A ROM the gate let through can still be refused by the core: the cart copy or the allocation failing, or romdb.c
    // knowing the cart only as a bigger image than the file handed over. BlastemMdSystem reports that by never
    // activating (it throws nothing, because the machine it failed to build has nothing to unwind), which is what this
    // turns into a refusal the planner shows.
    if (!sys->activated()) {
        refuse(why, "BlastEm refused the ROM");
        return nullptr;
    }
    sys->enableStateSnapshot();  // republish a tear-free savestate for the control plane's readState/readSram
    return sys;                  // implicit upcast to unique_ptr<SystemBase>
}

}  // namespace rp::blastem
