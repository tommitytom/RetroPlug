#pragma once

// The device check (docs/modular-graph-phase-e1-design.md section 7): every node type a product registers, created
// from its default settings as msgpack into memory the caller owns, activated, run for one block through the executor
// and destroyed, with no heap and no exceptions. rp-device-check runs it on the RP2350 (main.cpp, beside this file),
// where the image's link trap is what proves the path allocates and throws nothing; retroplug-device-mode-test runs
// the same code on the desktop over the device-mode builds of the same packages, where it is what proves the path
// works and where the counting allocator stands in for the trap.
//
// It is a host, not a node: it reaches every type through the C ABI alone, and it plans nothing. What a planner would
// have worked out (each port's kind and channel count, each queue's capacity, each param's default) comes with the
// settings, from the desktop's build of the type (the case generator in cases/).
//
// Beside the types it runs a plan (runPlan, docs/modular-graph-phase-e3-design.md section 7): a multi-node plan the
// desktop exported for the RP2350 (plan.json beside this file, plan.cmake), loaded by the plan loader in the same
// arena, run in ragged blocks with its params moving, and unloaded, which puts the loader and the executor's coupled
// groups and feedback copies under the image's trap, and holds the run, on the desktop, to what the test expects.

#include <cstddef>
#include <cstdint>

#include "graph/PlanLoader.hpp"
#include "graph/rp_node.h"

namespace rp::device_check {

// One port of a type's layout for its default settings, resolved as a planner would for a node with no wires: an
// unwired "any" input is mono, a match output takes its port's count.
struct PortCase {
    std::uint32_t kind = RP_PORT_AUDIO;  // rp_port_kind
    bool          output = false;
    std::uint32_t channels = 0;  // audio
    std::uint32_t messages = 0;  // events and bytes: the queue's capacity
    std::uint32_t bytes = 0;
};

// A registered type's case: its default settings as the msgpack a device reads, and the layout they give.
struct TypeCase {
    const char*          id = nullptr;
    const std::uint8_t*  settings = nullptr;
    std::uint32_t        settingsSize = 0;
    const PortCase*      ports = nullptr;
    std::uint32_t        portCount = 0;
    const double*        params = nullptr;  // each param's default, in layout order
    std::uint32_t        paramCount = 0;
};

// Where a run stopped. Ok is the only pass; anything else names the type (by its index in registration order, across
// packages) and what it failed.
enum class Step : std::uint8_t {
    Ok,
    NoCase,       // a registered type with no case: the cases were generated from another registration
    CaseUnused,   // a case no registered type took
    QueryMemory,  // query_memory refused the settings
    Arena,        // the instance, its buffers and its queues do not fit the arena
    Create,       // create refused the settings
    Activate,
    Process,      // the block faulted the node
};

struct Result {
    Step          step = Step::Ok;
    std::uint32_t type = 0;  // the type's index, in registration order
    const char*   id = nullptr;
    std::uint32_t types = 0;          // how many types ran to the end (on a pass, every one registered)
    std::size_t   instanceBytes = 0;  // the largest query_memory size among the types reached, the failed one included
    std::size_t   arenaBytes = 0;     // the most of the arena one type used, instance, buffers and queues together
    rp_error      error{};            // the type's own reason, where it gave one
};

// The one resource the check's host holds: a stereo audio buffer, res:1, which a case names where a type's defaults
// must name a resource and name none (the case generator's completions). Its frames are written by run(), so a type
// reads a signal from it, and a recorder may write it.
inline constexpr std::uint32_t kBufferResource = 1;
inline constexpr std::uint32_t kBufferChannels = 2;
inline constexpr std::uint32_t kBufferFrames = 256;

// What rp-device-check gives run(): one block of kBlockFrames, in an arena of kArenaBytes. The arena is sized for the
// largest default instance, rp.core.delay's (a second of stereo at 48 kHz, 384,000 bytes of delay line of its own),
// with room for its ports; runPlan's plan is loaded in the same arena afterwards, and is exported against it
// (plan.cmake's RP_DEVICE_CHECK_ARENA_BYTES, which the image and the test hold to this). retroplug-device-mode-test
// runs the check in an arena of the same size on the desktop, where pointers and max_align_t are wider, so the
// desktop's sizes should be an upper bound on the device's. For the plan the image checks it (PlanStep::Estimate,
// against the exporter's figure); for the per-type runs it prints the most a type used, and nothing holds it.
inline constexpr std::uint32_t kBlockFrames = 64;
inline constexpr std::size_t   kArenaBytes = 400 * 1024;

// The host every type is created under: no heap (the types it runs declare none), no guards, a resource table of the
// one buffer above, and device links of a host that owns no device (every acquire answers null). The case generator
// creates the desktop's builds of the types under it too.
const rp_host& host();

// Runs every type of every package in `packages` (rp_register_packages()'s list) against its case, one block of
// `frames` frames at 48 kHz each, with `arena` reused from one type to the next.
Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames);

// ---- the plan ----------------------------------------------------------------------------------------------------

// A param change the plan check pushes at frame 0 of block `block`: node `node`'s param `param`, in its layout's order.
struct PlanMove {
    std::uint32_t block = 0;
    const char*   node = nullptr;
    std::uint32_t param = 0;
    double        value = 0.0;
};

// What runPlan runs: an exported plan and the arena the exporter's sizing gave it (its header's <symbol>ArenaEstimate;
// 0 for none), how many frames, in what block sizes (taken in turn, each held to the plan's maxFrames and to what is
// left; none given, maxFrames each; a zero refused), the param moves, and the node whose rp.devtest.digest/1 the run
// must feed (null for none).
struct PlanCase {
    const std::uint8_t*  blob = nullptr;
    std::size_t          blobSize = 0;
    std::size_t          arenaEstimate = 0;
    std::uint32_t        frames = 0;
    const std::uint32_t* splits = nullptr;
    std::size_t          splitCount = 0;
    const PlanMove*      moves = nullptr;
    std::size_t          moveCount = 0;
    const char*          digest = nullptr;
};

// The image's run of its plan (plan.json): a second at 48 kHz, in ragged blocks from 1 frame to the plan's maxFrames
// (256), with the gain, both lockstep drives, the biquad's frequency, the constant and the delay's time moving. Its
// node ids and param positions are plan.json's.
inline constexpr std::uint32_t kPlanFrames = 48000;
inline constexpr std::uint32_t kPlanSplits[] = {1, 31, 256, 3, 200, 77, 128, 64, 255, 16};
inline constexpr PlanMove      kPlanMoves[] = {
    {2, "g", 0, -1.5}, {3, "a", 0, 1.9}, {5, "bq", 0, 700.0}, {6, "b", 0, 0.4},
    {8, "k", 0, 0.2},  {9, "d", 0, 300.0}, {11, "g", 0, 0.3}, {40, "bq", 0, 4000.0},
};
inline constexpr const char* kPlanDigest = "dg";
// What the digest hears when the desktop's builds run the same plan the same way: linux-x64 GCC 13's figure (the
// reference host parity and the kit and render baselines hold theirs to), pinned there by retroplug-device-mode-test,
// which fails with the new value when plan.json, a node it names or the case moves it. Another compiler or architecture
// may differ, as the M33 may: the image prints its own beside it and does not judge it, the float tolerance between
// the device and the desktop being the audio version's.
inline constexpr std::uint64_t kPlanDesktopDigest = 0xb8854b2cc247318full;

// Where a plan run stopped. Ok is the only pass; each other step names the node at fault in `error.node` ("-" for
// none: a blob refused whole, a case's own fault) and says what happened in `error.message`.
enum class PlanStep : std::uint8_t {
    Ok,
    Load,      // the loader refused the plan, or a node failed part-way through its run-up (unwound): its error
    Move,      // a move names no param stream the plan has (no such node, a param past its params, or one a wire
               // drives), or its stream is full
    Process,   // a block faulted a node (the executor silences it from then on)
    Result,    // the run did not hold: a move never pushed (past the last block), a queue dropped messages, or the
               // case's digest node missing, deaf, or counting other blocks than ran
    Estimate,  // the loaded plan took more arena than the exporter's sizing gave it, which the design takes as an upper
               // bound on the device's (its section 11, question 1)
    Case,      // the case cannot run: a block size of 0
};

struct PlanResult {
    PlanStep                 step = PlanStep::Ok;
    rp::graph::PlanLoadError error{};  // the loader's for Load; for every other step, the node and what happened
    std::uint32_t            nodes = 0;
    std::size_t              arenaBytes = 0;  // what the loaded plan took of the arena
    double                   sampleRate = 0.0;
    std::uint32_t            maxFrames = 0;
    std::uint32_t            blocks = 0;  // blocks run (on a Process failure, the one that faulted included)
    std::uint32_t            frames = 0;
    std::uint32_t            moves = 0;  // param changes pushed
    std::uint32_t            drops = 0;  // messages the plan's full queues dropped
    // What the case's digest node (rp.devtest.digest/1) had seen at the end, when the plan has the node it names: its
    // FNV-1a 64 of everything that reached it, the blocks it counted, and whether any of it was more than silence.
    bool                     digested = false;
    std::uint64_t            digest = 0;
    std::uint32_t            digestBlocks = 0;
    bool                     heard = false;
};

// Loads `c.blob` into `arena` with the loader, over the node types of `packages` (rp_register_packages()'s list) under
// host(), holds its arena to the case's estimate, runs it as `c` says, judges the run (PlanStep::Result) and unloads
// it. Fills `out`: a result is a few hundred bytes, so the caller holds it rather than this frame, and every one of its
// bytes is zero by default, so the image's static result lands in .bss and costs no initialised RAM. Allocates nothing.
void runPlan(const PlanCase& c, const rp_package* const* packages, std::size_t packageCount, void* arena,
             std::size_t arenaBytes, PlanResult* out);

}  // namespace rp::device_check
