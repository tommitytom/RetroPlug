#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanLoader.hpp"
#include "graph/Planner.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// The exporter (docs/modular-graph-phase-e3-design.md section 4): a graph document planned for a device and written as
// a plan blob (PlanBlob.hpp), which the device's loader (PlanLoader.hpp) lays out in its arena. Desktop only.
struct ExportOptions {
    Target        target = Target::Pico;  // the one target a plan is exported for
    double        sampleRate = 48000.0;
    std::uint32_t maxFrames = 256;
    std::size_t   arenaBytes = 0;  // the device's arena; 0 sizes the plan and refuses nothing for its size
    // Load the blob once with the device's builds before handing it back (exportPlan's comment). Off only for a test
    // that needs a blob the device refuses.
    bool          trialLoad = true;
};

struct ExportResult {
    std::vector<PlanError>    errors;
    std::vector<std::uint8_t> blob;  // empty when errors is not
    PlanSize                  size;  // what the loader's sizing pass gives the blob with the device's builds
    bool                      ok() const { return errors.empty(); }
};

// Plans `document` with the planner for `options.target`, so every rule a desktop plan keeps applies (the wiring, the
// layouts, unique types, the heap and threads refusals), on a host of the exporter's own: its rp.host.devices/2 answers
// every acquire with null, it has no resources, so a document that names one is refused ("resources are not exported
// to rp2350 yet"), and it sets a package with memory of its own (a script pack) up in a block of its own
// (rp.host.packages/1, graph/PackageMemory.hpp). Writes each node's settings from its parsed settings
// (rp.sdk.settings-json/1, then settingsToMsgpack), never the document's JSON, so the device's reader agrees with the
// desktop's by construction. Then runs the loader's own check and sizing pass (sizePlan) over the blob with
// `deviceTypes`, the device's builds of the types (on the desktop, the renamed device-mode objects:
// rp_register_device_mode_packages), so a type the device does not build, one that asks more alignment than the target
// gives, or a plan its arena cannot hold (its packages' memory among what it holds), is refused here as the device
// would refuse it. Then the trial load: the blob loaded with the same builds on the exporter's host, one block of
// `options.maxFrames` run with every input empty, and unloaded, so a create or activate only the device's build
// refuses fails the export too, and so does a script node whose first block faults (graph/rp_script.h's
// rp.script.fault/1: a script node is made at its first block, where its pack's heap is also sized for the plan's
// queues). The exporter's host offers a clock (rp.host.clock/1: the thread's CPU time at the desktop's deadline), so a
// script node that runs on there faults rather than hanging the export. What it cannot see is what only a real device
// answers: the box's own host interfaces, its links, its timing (its clock, and its heap for a block's contents).
ExportResult exportPlan(const GraphDocument& document, const NodeRegistry& registry,
                        std::span<const rp_package* const> deviceTypes, const ExportOptions& options);
// The same, from the document's JSON ("document" refuses text that is not one, as GraphHost::setGraph does).
ExportResult exportPlan(std::string_view documentJson, const NodeRegistry& registry,
                        std::span<const rp_package* const> deviceTypes, const ExportOptions& options);

}  // namespace rp::graph
