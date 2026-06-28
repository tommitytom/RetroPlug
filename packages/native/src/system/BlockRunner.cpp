#include "system/BlockRunner.hpp"

#include <memory>
#include <vector>

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/LinkGroup.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

// Slot of a link member within project.systems(). Linear scan — the system
// count is tiny and this is alloc-free (no per-block heap on the audio thread).
// A link member is always present in the systems list, so this always resolves.
std::size_t slotOf(const std::vector<std::unique_ptr<SystemBase>>& systems,
                   const SameBoySystem* needle) {
    for (std::size_t i = 0; i < systems.size(); ++i) {
        if (systems[i].get() == static_cast<const SystemBase*>(needle)) return i;
    }
    return 0;
}

} // namespace

void runBlock(const AudioBlockInfo& info, Project& project, const AudioRouter& router) {
    auto& systems = project.systems();

    // Unlinked pass: every system gets its routed bus. Linked SameBoys self-bail
    // (their LinkGroup drives them below, so they no-op here — double-processing
    // is impossible); Mesen / standalone systems produce audio here.
    for (std::size_t i = 0; i < systems.size(); ++i) {
        SystemBase* sys = systems[i].get();
        if (!sys) continue;
        const AudioBus b = router.bus(i);
        float* outs[2] = { b.l, b.r };
        sys->onProcess(info, outs);
    }

    // Linked pass: step each group's members in lockstep (round-robin GB_run so
    // their serial-bit handshake stays in sync mid-block), then finish each
    // member into its OWN routed bus. This is what LinkGroup::onProcess could not
    // do — it hard-coded outs[0]/[1] for every member.
    for (const LinkGroup& group : project.linkGroups()) {
        const auto& members = group.members();
        if (members.empty()) continue;

        for (SameBoySystem* m : members)
            if (m) m->prepareForBlock(info);

        bool anyBelow = true;
        while (anyBelow) {
            anyBelow = false;
            for (SameBoySystem* m : members)
                if (m && m->stepIfBelowTarget(info.frames)) anyBelow = true;
        }

        for (SameBoySystem* m : members) {
            if (!m) continue;
            const AudioBus b = router.bus(slotOf(systems, m));
            float* outs[2] = { b.l, b.r };
            m->finishBlock(info, outs);
        }
    }
}
