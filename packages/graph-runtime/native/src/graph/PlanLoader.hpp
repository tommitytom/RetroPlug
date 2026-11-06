#pragma once

// The loader (docs/modular-graph-phase-e3-design.md section 4): an exported plan (PlanBlob.hpp) laid out in an arena
// the caller owns, its nodes created and run up, and handed to the executor as a PlanView. Portable: it allocates
// nothing, throws nothing and reaches every node through the C ABI, so the device runs it as the desktop does; the
// desktop's exportPlan runs its sizing pass (sizePlan) over the blob it wrote, with the device's builds of the types,
// so a plan that would not fit is refused at export by the same arithmetic the device refuses it with.
//
// loadPlan, in order: checks the blob (its magic, its version, its length, its CRC, every index and offset in bounds,
// every type id registered), then lays it out and sizes it in one pass (each node's own query_memory on its settings,
// each package's own memory, plus every pool, view array, step and counter), refusing a plan over the arena, naming
// the package or the node that tips it over and by how much, before it creates anything; then sets up each package
// that has memory of its own and runs the plan up in PlanStorage::adopt's order: create and activate each node in the
// document's order, bind the steps, tell each linked node its peers, each node its connected outputs, and start each
// param the document sets. A failure part-way unwinds what it made, in reverse, and names the node (or the package) and
// the step. unloadPlan deactivates and destroys the nodes in reverse, then tears the packages down.
//
// Package memory (rp_package_memory, docs/modular-graph-phase-e4-design.md section 5): the packages the plan's node
// types come from are found from the types themselves (the blob names no package), and each that has memory of its own
// is sized once, in the same pass as everything else, and placed after the plan's own structures and before the nodes,
// in the order the plan first names one of its types. Its block is set up before any node is created and torn down
// after the last is destroyed. The nodes reach it through the host they are created under: the loader creates them
// under a host of its own, in the arena, which answers rp.host.packages/1 from the plan's packages and hands every
// other call to the caller's host.
//
// The blob is read in place and must outlive the plan loaded from it (on the device it is in flash): node ids and the
// masks point into it. The arena must be aligned to kArenaAlign, and a node type that asks for more than the target's
// alignment (PlanBlob.hpp's kTargetAlign, the RP2350's max_align_t) is refused, on the desktop's sizing pass as on the
// device. The sizing pass and the layout are one function (layout()), so on one host the measure is the placement.
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
#include "graph/PlanBlob.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// What an arena's start must be aligned to: the target's alignment (blob::kTargetAlign), and the compiler's max_align_t
// where that is more (the desktop's), so every request the layout makes (an instance's at most kTargetAlign, a pool's
// at most its type's) is met by an offset of the same alignment. A firmware's static arena is declared with it.
inline constexpr std::uint64_t kArenaAlign =
    blob::kTargetAlign > alignof(std::max_align_t) ? blob::kTargetAlign : alignof(std::max_align_t);

// Why a plan was refused: a code (the planner's where it has one: "unknown-type", "settings-invalid", "create-failed",
// "activate-failed", "link-without-step"; the loader's own: "plan-format", "plan-version", "plan-truncated",
// "plan-crc", "plan-target", "plan-malformed", "arena", "node-memory", "package-memory", "package-setup",
// "peers-without-link"), the node's id where one is at fault (cut to fit), and a sentence, which names the package
// where one is at fault.
struct PlanLoadError {
    char code[32];
    char node[64];
    char message[256];
};

// What a plan needs of an arena: `bytes` in all, of which `structureBytes` are its own structures (its buffers, queues,
// streams, steps and views, first in the arena), `instanceBytes` its nodes' own instances and `packageBytes` its
// packages' own memory (rp_package_memory).
struct PlanSize {
    std::size_t   bytes = 0;
    std::size_t   structureBytes = 0;
    std::size_t   instanceBytes = 0;
    std::size_t   packageBytes = 0;
    std::uint32_t nodes = 0;
    std::uint32_t packages = 0;  // the packages with memory of their own that it sets up
};

// A package with memory of its own that a loaded plan set up (rp_package_memory), in the arena.
struct LoadedPackage {
    const rp_package* package = nullptr;
    void*             memory = nullptr;
    std::size_t       bytes = 0;
    bool              setUp = false;

    // The name its memory hook gives it, for what is said of it.
    std::string_view name() const;
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
    std::span<LoadedPackage>   packages;   // those with memory of their own, in the order the plan first names them

    // The node with this id, or null.
    LoadedNode* find(std::string_view id) const;
    // Node `n`'s param `k` (in its layout's order): its own control stream, which a host writes a change into before a
    // block (pushControl). Null for a param a wire drives (it reads the wire's source, another node's output, which the
    // desktop's setParam refuses to write), and past the node's params.
    rp_control_queue* param(std::uint32_t n, std::uint32_t k) const;

    rp_control_queue* const* paramViews = nullptr;  // every step's param views, in step order
    const std::uint8_t*      wired = nullptr;       // per param view: a wire drives it

    // The host the nodes were created under: the loader's own, which answers rp.host.packages/1 from `packages` and
    // hands every other call to `outer`, the host loadPlan was given.
    rp_host                  host{};
    rp_host_packages         packageService{};
    const rp_host*           outer = nullptr;
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
