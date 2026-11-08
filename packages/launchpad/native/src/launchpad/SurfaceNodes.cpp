// graph/node.hpp first: its device-mode check reads the guards of what the translation unit included BEFORE it, and the
// SDK's own headers are what a device build may carry (the link trap still refuses a heap call from anywhere).
#include "graph/node.hpp"

#include "launchpad/SurfaceNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "graph/rp_device.h"
#include "launchpad/SurfaceLink.hpp"

namespace rp::launchpad {

namespace {

// The messages one drain hands back. The link keeps what a batch had no room for (it never drops on the consumer's
// behalf), so the node drains again until the link has nothing left, in its own storage: a block neither allocates nor
// grows the stack by the ring's whole depth.
constexpr std::size_t kSurfaceDrainBatch = 16;

// What a control stream can carry of a counter. A control value is a double end to end, and a message count that runs
// for days would eventually pass 2^53 and stop being exact. 2^24 is the point where a shared surface's traffic (a pad
// press a millisecond, a full repaint a second) cannot reach in one session, and a reader only ever compares a value
// with the previous block's, so the wrap is invisible: a count that comes back to a value it had 16.7 million messages
// ago reads as a change, which is what a repaint needs anyway.
double countOf(std::uint64_t n) { return static_cast<double>(n & 0xffffffu); }

// The generation is a control stream too, but 0 already means "nothing attached", so a claim's number is mapped into
// 1..2^24-1: a generation that wraps reads as the claim it once was rather than as a device going away.
double generationOf(std::uint32_t n) { return n == 0 ? 0.0 : static_cast<double>((n - 1) % 0xffffffu + 1); }

// What both surface nodes share: the host's one surface link, borrowed for the life of the plan. It is acquired in
// activate, kept across a reactivate (activate runs again on the same instance, so a link already held is not taken
// twice), and released in the destructor, the plan's reclaim, on the control thread. Placed by a setting rather than by
// the device's state, so both always activate: in a host with no surface (its acquire answers null, and the node runs
// on a NoSurface of its own), or with one unplugged, the link reads generation 0 and takes every push nowhere, and
// nothing in the graph changes when the device comes or goes.
struct SurfaceNode {
    const rp_host_devices* devices = nullptr;
    SurfaceLink*           surface = nullptr;  // the host's link, or `none`; set by the first activate
    void*                  held    = nullptr;  // what the acquire answered, handed back to the release
    NoSurface              none;
    // Set when activate bound the link, so the next process is the node's first block with it. A reactivate that keeps
    // the handle does not set it: that block has a reader on both sides of it, so nothing waited unread.
    bool                   fresh = false;

    void bind_host(const rp_host* h) noexcept {
        devices = static_cast<const rp_host_devices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
    }
    bool activate(double, std::uint32_t) {
        if (!devices) return false;
        if (!surface) {
            held    = devices->acquire(devices->ctx, kDeviceName, kSurfaceLinkInterface);
            surface = held ? static_cast<SurfaceLink*>(held) : &none;
            fresh   = true;
        }
        return true;
    }
    ~SurfaceNode() {
        if (devices && held) devices->release(devices->ctx, held);
    }
};

// The device's side of a controller session (docs/modular-graph-phase-c5-design.md section 5): what the Launchpad sent
// since the last block, the generation of the claim it came from and what the link lost. It only ever reads the link,
// so a session reading it runs before the node that follows it, and the plan holds no cycle and no feedback wire.
struct SurfaceIn : SurfaceNode {
    static constexpr const char* id       = "rp.launchpad.surface-in";
    static constexpr const char* name     = "Launchpad In";
    static constexpr const char* category = "device";
    // One device, one link: the planner refuses a second node of this type in a plan. No threads: the link's own
    // callback thread and writer thread are the provider's, so the type plans on a target that has none.
    static constexpr bool unique = true;

    // Named so `in`, `connected` and `drops` align below, and the queue's two parts stay one type.
    using InQueue = rp::events_out::sized<rp::capacity{.messages = kSurfaceInMessages, .bytes = kSurfaceInBytes}>;
    struct {
        InQueue         in;
        rp::control_out connected;
        rp::control_out drops;
    } outputs;
    // What one drain returns, in the node's own storage.
    std::array<DeviceMessage, kSurfaceDrainBatch> batch{};

    void process(const rp::Block&) noexcept {
        // The node's first block after it binds the link discards what the link already holds: those messages were sent
        // while no node read the device (the project's controller was off), so delivering them would replay presses
        // into a session that has only just started. Drained through the same batch until empty, never forwarded, and
        // allocation-free; a reactivate that keeps the handle does not discard.
        const bool first = fresh;
        fresh            = false;
        // Both counters are control streams from frame 0: a change at frame 0 on the block it happens, since a
        // stream's start value is the last block's end. The generation replaces a flag: a pair swapped inside a block
        // changes it, so a session sees a new device rather than none.
        const double generation = generationOf(surface->generation());
        if (generation != outputs.connected.queue->start) outputs.connected.push(0, generation);
        const double drops = countOf(surface->dropped());
        if (drops != outputs.drops.queue->start) outputs.drops.push(0, drops);
        // What the device sent since the last drain goes into `in` at frame 0, a control surface's timing being no
        // musical one. A message the queue has no room for is dropped and counted by the queue
        // (rp_queue::dropped), never allocated for, and the link stays drained either way: each call takes what it
        // answers, so a full batch means the ring may hold more.
        for (;;) {
            const std::size_t n = surface->drainInput(batch.data(), batch.size());
            if (!first)
                for (std::size_t i = 0; i < n; ++i)
                    outputs.in.push(0, std::span<const std::uint8_t>(batch[i].data, batch[i].size));
            if (n < batch.size()) break;
        }
    }
};

// The session's side: everything it sends the device, handed over in queue order in the block it queued it.
struct SurfaceOut : SurfaceNode {
    static constexpr const char* id       = "rp.launchpad.surface-out";
    static constexpr const char* name     = "Launchpad Out";
    static constexpr const char* category = "device";
    // As the input node's: two output nodes would split the device's LED frames.
    static constexpr bool unique = true;

    struct {
        rp::events_in out;
    } inputs;

    void process(const rp::Block&) noexcept {
        // The link owns the send ring, and counts (in dropped()) a push that went nowhere: nothing attached, or no room
        // left in its ring.
        for (const rp_message& m : inputs.out.messages()) {
            const auto data = inputs.out.data(m);
            surface->pushOutput(data.data(), data.size());
        }
    }
};

}  // namespace

const rp_package* launchpadPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<SurfaceIn>(), rp::node_type<SurfaceOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.launchpad","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::launchpad

extern "C" const rp_package* rp_launchpad_package(void) { return rp::launchpad::launchpadPackage(); }
