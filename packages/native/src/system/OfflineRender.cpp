#include "system/OfflineRender.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <TaskScheduler.h>

#include "project/Project.hpp"
#include "system/BlockRunner.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo
#include "system/sameboy/LinkGroup.hpp"

namespace {

// One worker job = one render unit (a singleton system or a link group). Renders
// the whole timeline for its members into their own per-slot output buffers, one
// block at a time. m_SetSize == 1 so the whole unit runs on one worker thread —
// required so a Mesen unit's emulation thread stays fixed across the render.
class UnitRenderTask : public enki::ITaskSet {
public:
    UnitRenderTask(std::vector<SystemBase*>                          members,
                   std::vector<std::size_t>                          slots,
                   const std::vector<std::unique_ptr<SystemBase>>&   systems,
                   const AudioRouter&                                router,
                   float* const*                                     ls,
                   float* const*                                     rs,
                   std::vector<std::vector<float>>&                  out,
                   const OfflineRenderParams&                        params)
        : members_(std::move(members)), slots_(std::move(slots)),
          systems_(&systems), router_(&router), ls_(ls), rs_(rs),
          out_(&out), p_(&params) {
        m_SetSize = 1;
    }

    void ExecuteRange(enki::TaskSetPartition, std::uint32_t) override {
        const std::uint32_t blockSize = p_->blockSize;
        double ppq = p_->startPpq;   // accumulated identically per unit -> deterministic

        for (std::uint64_t s = 0; s < p_->totalSamples; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, p_->totalSamples - s));
            const AudioBlockInfo info{ frames, p_->sampleRate, p_->bpm, ppq,
                                       p_->transportPlaying };

            // Zero this unit's slot scratch (members SUM into the bus).
            for (std::size_t slot : slots_) {
                std::fill_n(ls_[slot], frames, 0.0f);
                std::fill_n(rs_[slot], frames, 0.0f);
            }

            runUnit(info, members_.data(), members_.size(), *systems_, *router_);

            // Append each member's block into its whole-song output buffer.
            for (std::size_t slot : slots_) {
                std::vector<float>& dst = (*out_)[slot];
                for (std::uint32_t f = 0; f < frames; ++f) {
                    dst.push_back(ls_[slot][f]);
                    dst.push_back(rs_[slot][f]);
                }
            }

            if (p_->transportPlaying)
                ppq += (p_->bpm / 60.0) * (static_cast<double>(frames) / p_->sampleRate);
        }
    }

private:
    std::vector<SystemBase*>                        members_;
    std::vector<std::size_t>                        slots_;   // global slot per member
    const std::vector<std::unique_ptr<SystemBase>>* systems_;
    const AudioRouter*                              router_;
    float* const*                                   ls_;
    float* const*                                   rs_;
    std::vector<std::vector<float>>*                out_;
    const OfflineRenderParams*                      p_;
};

} // namespace

std::vector<std::vector<float>> renderUnitsParallel(Project& project,
                                                    const OfflineRenderParams& params) {
    auto& systems = project.systems();
    const std::size_t n = systems.size();
    std::vector<std::vector<float>> out(n);
    if (n == 0 || params.totalSamples == 0) return out;

    // Reserve each per-system output (interleaved L/R) so the per-block push_back
    // never reallocates (each is written by exactly one worker thread).
    for (auto& v : out) v.reserve(static_cast<std::size_t>(params.totalSamples) * 2);

    // Per-slot block scratch + a PerSystemRouter shared across units. Each slot
    // belongs to exactly one unit -> one worker, so all access stays disjoint.
    const std::uint32_t blockSize = params.blockSize;
    std::vector<std::vector<float>> bl(n, std::vector<float>(blockSize, 0.0f));
    std::vector<std::vector<float>> br(n, std::vector<float>(blockSize, 0.0f));
    std::vector<float*> ls(n), rs(n);
    for (std::size_t i = 0; i < n; ++i) { ls[i] = bl[i].data(); rs[i] = br[i].data(); }
    PerSystemRouter router(ls.data(), rs.data());

    auto slotsOf = [&](const std::vector<SystemBase*>& members) {
        std::vector<std::size_t> slots;
        slots.reserve(members.size());
        for (SystemBase* m : members)
            for (std::size_t i = 0; i < n; ++i)
                if (systems[i].get() == m) { slots.push_back(i); break; }
        return slots;
    };

    // Partition into units, mirroring runBlock: each link group is a unit; each
    // unlinked system is a singleton unit.
    std::vector<std::unique_ptr<UnitRenderTask>> tasks;
    tasks.reserve(n);

    for (const LinkGroup& group : project.linkGroups()) {
        const auto& membersBase = group.membersBase();
        if (membersBase.empty()) continue;
        std::vector<SystemBase*> members(membersBase.begin(), membersBase.end());
        auto slots = slotsOf(members);
        tasks.push_back(std::make_unique<UnitRenderTask>(
            std::move(members), std::move(slots), systems, router,
            ls.data(), rs.data(), out, params));
    }
    for (std::size_t i = 0; i < n; ++i) {
        SystemBase* sys = systems[i].get();
        if (!sys || sys->isLinked()) continue;
        tasks.push_back(std::make_unique<UnitRenderTask>(
            std::vector<SystemBase*>{ sys }, std::vector<std::size_t>{ i },
            systems, router, ls.data(), rs.data(), out, params));
    }

    enki::TaskScheduler scheduler;
    scheduler.Initialize();   // one worker per logical CPU minus one
    for (auto& t : tasks) scheduler.AddTaskSetToPipe(t.get());
    scheduler.WaitforAllAndShutdown();   // blocks until every unit is done

    return out;
}
