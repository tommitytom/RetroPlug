#pragma once

// The device check (docs/modular-graph-phase-e1-design.md section 7): every node type a product registers, created
// from its default settings as msgpack into memory the caller owns, activated, run for one block through the executor
// and destroyed, with no heap and no exceptions. rp-device-check runs it on the RP2350 (main.cpp, beside this file),
// where the image's link trap is what proves the path allocates and throws nothing; retroplug-device-mode-test runs
// the same code on the desktop over the device-mode builds of the same packages, where it is what proves the path
// works and where the counting allocator stands in for the trap.
//
// It is a host, not a node: it reaches every type through the C ABI alone, as E3's arena executor will, and it plans
// nothing. What a planner would have worked out (each port's kind and channel count, each queue's capacity, each
// param's default) comes with the settings, from the desktop's build of the type (the case generator in cases/).

#include <cstddef>
#include <cstdint>

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
// with room for its ports. retroplug-device-mode-test runs the check in an arena of the same size on the desktop, where
// pointers and max_align_t are wider, so the desktop's sizes should be an upper bound on the device's: they are for
// every type the check runs today, but nothing checks it for a new one (the image is not run before E3).
inline constexpr std::uint32_t kBlockFrames = 64;
inline constexpr std::size_t   kArenaBytes = 400 * 1024;

// The host every type is created under: no heap (the types it runs declare none), no guards, and a resource table of
// the one buffer above. The case generator creates the desktop's builds of the types under it too.
const rp_host& host();

// Runs every type of every package in `packages` (rp_register_packages()'s list) against its case, one block of
// `frames` frames at 48 kHz each, with `arena` reused from one type to the next.
Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames);

}  // namespace rp::device_check
