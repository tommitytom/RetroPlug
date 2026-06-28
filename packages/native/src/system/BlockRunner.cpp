#include "system/BlockRunner.hpp"

#include <memory>
#include <vector>

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/LinkGroup.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

// Slot of a member within project.systems(). Linear scan — the system count is
// tiny and this is alloc-free (no per-block heap on the audio thread). A unit
// member is always present in the systems list, so this always resolves.
std::size_t slotOf(const std::vector<std::unique_ptr<SystemBase>>& systems,
                   const SystemBase* needle) {
    for (std::size_t i = 0; i < systems.size(); ++i) {
        if (systems[i].get() == needle) return i;
    }
    return 0;
}

} // namespace

void runUnit(const AudioBlockInfo& info,
             SystemBase* const* members, std::size_t count,
             const std::vector<std::unique_ptr<SystemBase>>& systems,
             const AudioRouter& router) {
    // Prepare every member for the block.
    for (std::size_t k = 0; k < count; ++k)
        if (members[k]) members[k]->prepareForBlock(info);

    // Round-robin step until all members reach the block target. For a link
    // group this interleaves GB_run() across members so their serial-bit
    // handshake stays in sync mid-block; for a singleton it just steps to done.
    bool anyBelow = true;
    while (anyBelow) {
        anyBelow = false;
        for (std::size_t k = 0; k < count; ++k)
            if (members[k] && members[k]->stepIfBelowTarget(info.frames)) anyBelow = true;
    }

    // Finish each member into its OWN routed bus (members SUM into it).
    for (std::size_t k = 0; k < count; ++k) {
        SystemBase* m = members[k];
        if (!m) continue;
        const AudioBus b = router.bus(slotOf(systems, m));
        float* outs[2] = { b.l, b.r };
        m->finishBlock(info, outs);
    }
}

void runBlock(const AudioBlockInfo& info, Project& project, const AudioRouter& router) {
    auto& systems = project.systems();

    // Singleton units: every unlinked system as a 1-member unit. Linked SameBoys
    // are skipped here and driven by their LinkGroup below — keeping the
    // link-vs-singleton decision in one place. Every backend flows through the
    // same triad (via runUnit).
    for (std::size_t i = 0; i < systems.size(); ++i) {
        SystemBase* sys = systems[i].get();
        if (!sys || sys->isLinked()) continue;
        runUnit(info, &sys, 1, systems, router);
    }

    // Link-group units: members stepped in lockstep, each finished into its own
    // routed bus.
    for (const LinkGroup& group : project.linkGroups()) {
        const auto& members = group.membersBase();
        if (members.empty()) continue;
        runUnit(info, members.data(), members.size(), systems, router);
    }
}
