#include "BuiltInPlan.hpp"

#include <cstdio>

#include "graph/Executor.hpp"
#include "graph/noinline.hpp"
#include "graph/rp_script.h"

namespace rp::box {

namespace {

// A NUL-terminated field of the loader's error (each is cut to fit, and terminated).
const char* text(const char* field) { return field[0] ? field : "-"; }

}  // namespace

bool BuiltInPlan::load(std::span<const std::uint8_t> blob, std::span<std::byte> arena,
                       std::span<const rp_package* const> types, const rp_host* host, Print print, Micros micros) {
    print_ = print;
    micros_ = micros;
    const std::uint64_t start = micros_ ? micros_() : 0;
    plan_ = graph::loadPlan(blob, arena, types, host, &error_);
    loadMicros_ = micros_ ? micros_() - start : 0;
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
    for (const graph::LoadedPackage& pk : plan_->packages) {
        std::snprintf(line, sizeof line, "[host]   pack %.*s, %lu bytes", static_cast<int>(pk.name().size()),
                      pk.name().data(), static_cast<unsigned long>(pk.bytes));
        print_(line);
    }
}

// What each script pack's context holds once the first block has made its nodes' port pools: its heap for this plan
// (MicroQuickJS's own and the live objects, collected, of the context's bytes), and how long the load (which sets the
// packs up and makes their nodes' instances) and that block took.
RP_NOINLINE void BuiltInPlan::printPackages(std::uint64_t firstBlockMicros) const {
    char line[160];
    for (const graph::LoadedPackage& pk : plan_->packages) {
        for (const graph::LoadedNode& n : plan_->nodes) {
            bool ours = false;
            for (std::uint32_t t = 0; t < pk.package->node_type_count && !ours; ++t)
                ours = pk.package->node_types[t] == n.type;
            const auto* heap =
                ours ? static_cast<const rp_script_heap*>(n.node->vt->get_interface(n.node, RP_SCRIPT_HEAP_INTERFACE))
                     : nullptr;
            rp_script_heap_use use{};
            if (!heap || !heap->use(n.node, true, &use)) continue;
            std::snprintf(line, sizeof line,
                          "[host]   pack %.*s: heap %lu of %lu bytes in use after the first block; the load took %lu "
                          "us and the first block %lu us",
                          static_cast<int>(pk.name().size()), pk.name().data(),
                          static_cast<unsigned long>(use.fixed + use.heap), static_cast<unsigned long>(use.bytes),
                          static_cast<unsigned long>(loadMicros_), static_cast<unsigned long>(firstBlockMicros));
            print_(line);
            break;
        }
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
    const std::uint64_t start = !ranOnce_ && micros_ ? micros_() : 0;
    graph::runBlock(plan_->view, frames, transport);
    if (!ranOnce_) {
        ranOnce_ = true;
        if (print_ && !plan_->packages.empty()) printPackages(micros_ ? micros_() - start : 0);
    }
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
            // A script node says why (graph/rp_script.h): its first fault's words.
            const auto* fault =
                static_cast<const rp_script_fault*>(n.node->vt->get_interface(n.node, RP_SCRIPT_FAULT_INTERFACE));
            const char* words = fault ? fault->reason(n.node) : nullptr;
            if (words) {
                std::snprintf(line, sizeof line, "[host]   %s", words);
                print_(line);
            }
        }
    }
    if (named < faults && print_) {
        std::snprintf(line, sizeof line, "[host] %lu more nodes faulted", static_cast<unsigned long>(faults - named));
        print_(line);
    }
    faultsSeen_ = faults;
}

}  // namespace rp::box
