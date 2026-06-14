#include "system/sameboy/LinkGroup.hpp"

#include "system/sameboy/SameBoySystem.hpp"

void LinkGroup::onProcess(const AudioBlockInfo& info, float* const* outs) {
    if (members_.empty()) return;

    // Prepare each member's audio accumulator. mixInto reads `audioFrameCount_`
    // and the buffer SameBoySystem fills via its audio callback while GB_run
    // ticks; we drive that loop here instead of letting each member onProcess
    // run independently.
    for (auto* sys : members_) {
        if (sys) sys->prepareForBlock(info);
    }
    
    // TODO: This might be overkill

    // Round-robin GB_run() across members. Each call advances one CPU
    // instruction (~2–32 cycles), well below the LSDJ serial-bit timing
    // tolerance, so the cross-instance bit ferrying inside
    // SameBoySystem::serialStart/serialEnd is effectively cycle-accurate.
    bool anyBelow = true;
    while (anyBelow) {
        anyBelow = false;
        for (auto* sys : members_) {
            if (sys && sys->stepIfBelowTarget(info.frames)) {
                anyBelow = true;
            }
        }
    }

    for (auto* sys : members_) {
        if (sys) sys->finishBlock(info, outs);
    }
}
