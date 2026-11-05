#pragma once

// A box's plan (docs/modular-graph-phase-e3-design.md sections 2 and 5): the blob its build exported from the
// product's plan.json and embedded in the image, loaded at boot into a static arena by the loader
// (graph/PlanLoader.hpp) and run one block a main-loop pass, as the block driver says. Another graph means another
// image (the E3 design's decision 1), so the plan is loaded once and never unloaded.
//
// What it says on the console: the plan it loaded (its nodes, and the arena bytes it takes of the arena's size), or
// why the loader refused it, after which it runs nothing; and a node the executor silences for faulting, once.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does; the product gives it the line printer.

#include <cstddef>
#include <cstdint>
#include <span>

#include "BlockDriver.hpp"
#include "graph/PlanLoader.hpp"

namespace rp::box {

class BuiltInPlan {
public:
    // One console line, without its newline.
    using Print = void (*)(const char* line);
    // The most nodes whose faults it tells apart (a fault past them is still counted, and printed without its name).
    static constexpr std::uint32_t kNodes = 64;

    constexpr BuiltInPlan() = default;

    // Loads `blob` (which must outlive the plan: node ids point into it) into `arena`, its nodes created under `host`
    // from `types` (the product's rp_register_packages()). True with the plan's line printed; false with the refusal's.
    bool load(std::span<const std::uint8_t> blob, std::span<std::byte> arena, std::span<const rp_package* const> types,
              const rp_host* host, Print print);
    // One block, when the driver has one due: true when it ran. Nothing without a plan.
    bool run(BlockDriver& driver);

    graph::LoadedPlan*          plan() const { return plan_; }
    const graph::PlanLoadError& error() const { return error_; }

private:
    void printLoaded(std::size_t arenaBytes) const;
    void printRefused() const;
    void reportFaults();

    graph::LoadedPlan*   plan_ = nullptr;
    graph::PlanLoadError error_{};
    Print                print_ = nullptr;
    std::uint32_t        faultsSeen_ = 0;
    bool                 told_[kNodes] = {};
};

}  // namespace rp::box
