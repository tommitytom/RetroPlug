#include "BuiltInPlan.hpp"

#include <cstdio>

#include "graph/Executor.hpp"
#include "graph/noinline.hpp"

namespace rp::box {

namespace {

// A NUL-terminated field of the loader's error (each is cut to fit, and terminated).
const char* text(const char* field) { return field[0] ? field : "-"; }

}  // namespace

bool BuiltInPlan::load(std::span<const std::uint8_t> blob, std::span<std::byte> arena,
                       std::span<const rp_package* const> types, const rp_host* host, Print print) {
    print_ = print;
    plan_ = graph::loadPlan(blob, arena, types, host, &error_);
    if (!print_) return plan_ != nullptr;
    if (plan_)
        printLoaded(arena.size());
    else
        printRefused();
    return plan_ != nullptr;
}

// Each line's buffer in a frame of its own, never inlined into load(), so it is not on the stack under the load (the
// deepest chain the box runs, before its interrupts are on).
RP_NOINLINE void BuiltInPlan::printLoaded(std::size_t arenaBytes) const {
    char line[160];
    std::snprintf(line, sizeof line,
                  "[host] plan loaded: %u nodes, %lu of %lu arena bytes, %lu Hz, %lu frames a block",
                  static_cast<unsigned>(plan_->nodes.size()), static_cast<unsigned long>(plan_->bytes),
                  static_cast<unsigned long>(arenaBytes), static_cast<unsigned long>(plan_->sampleRate),
                  static_cast<unsigned long>(plan_->maxFrames));
    print_(line);
    for (const graph::LoadedNode& n : plan_->nodes) {
        std::snprintf(line, sizeof line, "[host]   node %.*s (%s), %lu bytes", static_cast<int>(n.idSize), n.id,
                      n.type->id, static_cast<unsigned long>(n.memoryBytes));
        print_(line);
    }
}

RP_NOINLINE void BuiltInPlan::printRefused() const {
    char line[sizeof error_.code + sizeof error_.node + sizeof error_.message + 64];  // the refusal's, whole
    std::snprintf(line, sizeof line, "[host] the built-in plan was refused: %s %s: %s", text(error_.code),
                  text(error_.node), text(error_.message));
    print_(line);
    print_("[host] no plan runs, and nothing is forwarded");
}

bool BuiltInPlan::run(BlockDriver& driver) {
    if (!plan_) return false;
    std::uint32_t frames = 0;
    rp_transport  transport{};
    if (!driver.next(&frames, &transport)) return false;
    if (frames == 0 || frames > plan_->maxFrames) return false;  // a driver started for another plan: never run it
    graph::runBlock(plan_->view, frames, transport);
    if (plan_->faults.load(std::memory_order_relaxed) != faultsSeen_) reportFaults();
    return true;
}

// The executor bumps the plan's fault count once per node that faults and silences the node from then on; each is
// named here once.
void BuiltInPlan::reportFaults() {
    const std::uint32_t faults = plan_->faults.load(std::memory_order_relaxed);
    char                line[160];
    std::uint32_t       named = 0;
    for (std::uint32_t i = 0; i < plan_->nodes.size(); ++i) {
        const graph::LoadedNode& n = plan_->nodes[i];
        if (!n.faulted.load(std::memory_order_relaxed)) continue;
        if (i < kNodes && told_[i]) {
            ++named;
            continue;
        }
        if (i >= kNodes) continue;
        told_[i] = true;
        ++named;
        if (print_) {
            std::snprintf(line, sizeof line, "[host] node %.*s (%s) faulted: the executor silences it from here on",
                          static_cast<int>(n.idSize), n.id, n.type->id);
            print_(line);
        }
    }
    if (named < faults && print_) {
        std::snprintf(line, sizeof line, "[host] %lu more nodes faulted", static_cast<unsigned long>(faults - named));
        print_(line);
    }
    faultsSeen_ = faults;
}

}  // namespace rp::box
