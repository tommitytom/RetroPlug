#pragma once

// The loader (docs/modular-graph-phase-e3-design.md section 4): an exported plan (PlanBlob.hpp) laid out in an arena
// the caller owns, its nodes created and run up, and handed to the executor as a PlanView. Portable: it allocates
// nothing, throws nothing and reaches every node through the C ABI, so the device runs it as the desktop does; the
// desktop's exportPlan runs its sizing pass (sizePlan) over the blob it wrote, with the device's builds of the types,
// so a plan that would not fit is refused at export by the same arithmetic the device refuses it with.
//
// loadPlan, in order: checks the blob (its magic, its version, its length, its CRC, every index and offset in bounds,
// every type id registered), sizes it (each node's own query_memory on its settings, plus every pool, view array, step
// and counter) and refuses one over the arena, naming the node that tips it over and by how much, before it creates
// anything; then lays it out and runs it up in PlanStorage::adopt's order: create and activate each node in the
// document's order, bind the steps, tell each linked node its peers, each node its connected outputs, and start each
// param the document sets. A failure part-way unwinds what it made, in reverse, and names the node and the step.
// unloadPlan deactivates and destroys the nodes in reverse.
//
// The blob is read in place and must outlive the plan loaded from it (on the device it is in flash): node ids and the
// masks point into it. The arena must be aligned to the larger of the target's alignment (PlanBlob.hpp's kTargetAlign,
// the RP2350's max_align_t) and the compiler's max_align_t, and a node type that asks for more than the target's
// alignment is refused, on the desktop's sizing pass as on the device, so the sizing pass and the layout are one
// computation wherever they run.
//
// The blob is trusted to be the exporter's output for these builds of its types. The checks keep the loader's own
// reads and writes in bounds (every index, offset and count, overflow-safe) and refuse a corrupted blob (the CRC), but
// nothing here can hold a step's views to its node's layout: a device build of a type has no layout_for
// (node_device.hpp), and a node reads its inputs and outputs by its layout's ordinals without consulting the counts. A
// blob crafted with fewer views than a type has ports, or a queue where it has an audio port, would make that node
// read or write out of bounds. That holds while every plan a device runs is built into its firmware by the exporter
// (the E3 design's decision 1); a blob from outside the firmware (a plan file, a live upload) needs each step's port
// kinds and counts checked against something the device knows, or a signature.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "graph/Plan.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// Why a plan was refused: a code (the planner's where it has one: "unknown-type", "settings-invalid", "create-failed",
// "activate-failed", "link-without-step"; the loader's own: "plan-format", "plan-version", "plan-truncated",
// "plan-crc", "plan-target", "plan-malformed", "arena", "node-memory", "peers-without-link"), the node's id where one
// is at fault (cut to fit), and a sentence.
struct PlanLoadError {
    char code[32];
    char node[64];
    char message[256];
};

// What a plan needs of an arena: `bytes` in all, of which `instanceBytes` are its nodes' own instances.
struct PlanSize {
    std::size_t   bytes = 0;
    std::size_t   instanceBytes = 0;
    std::uint32_t nodes = 0;
};

// One node of a loaded plan. Its fault flag and drop counter are the executor's (PlanStep::faulted, OutputQueue::drops)
// and live here, in the arena.
struct LoadedNode {
    const char*                id = nullptr;  // into the blob, not NUL-terminated
    std::uint32_t              idSize = 0;
    const rp_node_type*        type = nullptr;
    rp_node*                   node = nullptr;
    void*                      memory = nullptr;
    std::size_t                memoryBytes = 0;
    std::uint32_t              step = 0;
    std::atomic<bool>          faulted{false};
    std::atomic<std::uint32_t> drops{0};
    bool                       active = false;

    std::string_view name() const { return {id, idSize}; }
};

struct LoadedPlan {
    PlanView                   view;       // what runBlock runs
    double                     sampleRate = 0.0;
    std::uint32_t              maxFrames = 0;  // the longest block it takes
    std::span<LoadedNode>      nodes;      // in the document's order
    std::size_t                bytes = 0;  // what the plan takes of the arena it was laid out in
    std::atomic<std::uint32_t> faults{0};  // nodes that have faulted
    std::atomic<std::uint32_t> drops{0};   // messages full output queues have dropped

    // The node with this id, or null.
    LoadedNode* find(std::string_view id) const;
    // Node `n`'s param `k` (in its layout's order): its own control stream, which a host writes a change into before a
    // block (pushControl). Null for a param a wire drives (it reads the wire's source, another node's output, which the
    // desktop's setParam refuses to write), and past the node's params.
    rp_control_queue* param(std::uint32_t n, std::uint32_t k) const;

    rp_control_queue* const* paramViews = nullptr;  // every step's param views, in step order
    const std::uint8_t*      wired = nullptr;       // per param view: a wire drives it
};

// Checks the blob and sizes it against an arena of `arenaBytes` (0: any size), creating nothing; `types` are the
// packages whose node types it may name (a product's rp_register_packages()). False, with `error` filled, for a blob
// loadPlan would refuse before it created a node. `size` is filled once every node's query_memory has answered, so on
// a pass and on an over-budget refusal, not on a refused blob, settings or alignment, or sizes that overflow.
bool sizePlan(std::span<const std::uint8_t> blob, std::span<const rp_package* const> types, std::size_t arenaBytes,
              PlanSize* size, PlanLoadError* error);

// Loads the blob into `arena` (see above) with its nodes created under `host`. The plan, at the arena's start, or null
// with `error` filled and nothing left created.
LoadedPlan* loadPlan(std::span<const std::uint8_t> blob, std::span<std::byte> arena,
                     std::span<const rp_package* const> types, const rp_host* host, PlanLoadError* error);

// Deactivates and destroys every node, in reverse. The arena is the caller's again afterwards.
void unloadPlan(LoadedPlan* plan);

}  // namespace rp::graph
