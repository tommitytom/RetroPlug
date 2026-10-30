#pragma once

// The engine services the engine's nodes ask the host for through rp_host.get_interface.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"

class SystemBase;
struct SystemBuildRequest;  // system/SystemFactory.hpp, which only the builders need in full

namespace rp::host {

// A system node building its core where it runs (docs/modular-graph-phase-c3-design.md section 5). [control]: build and
// destroy both run there, build inside the setGraph that activates the node and destroy at the reclaim that frees the
// plan which last ran it (or as a refused plan unwinds). A node's resources are pinned by its instance already, so a
// build reads bytes rather than files.
#define RP_HOST_SYSTEMS_INTERFACE "rp.host.systems/1"
struct HostSystems {
    void* ctx;
    // Build the core of the system `system`, from the ROM and seeds the node's resources hold and the config its
    // settings made. Null, with the reason in *err, for a ROM that console's gate refuses, a boot the core refuses, an
    // id that is live (held by a node of any plan not yet reclaimed) and a full snapshot pool. A core built here is
    // the node's until `destroy`: the host claimed its snapshot slot under `system` and keeps it among its live cores,
    // so the plan's table and the control plane see it at once.
    SystemBase* (*build)(void* ctx, std::uint64_t system, SystemBuildRequest request, rp_error* err);
    // The node is giving up a core it built: release its snapshot slot, leave the live cores and delete it.
    void (*destroy)(void* ctx, SystemBase* core);
};

// The engine's host I/O (docs/modular-graph-phase-b3a-design.md section 3.3), for the rp.host I/O nodes and the cart
// mirror: the hosts stage what they receive through the engine and drain what it sends after each block, and these
// move it between that staging and the plan's queues. [audio], though not allocation-free: midiOut and controllerOut
// each append one std::vector per message to the buffers the hosts drain, as the native kernel stage's sinks did before
// B3a. A flat arena, as the staged host MIDI's is (Engine::Staging), is the fix.
//
// Host services are looked up by this versioned id so that a change of shape is visible to a node built against the
// old one. /2: the drop counter left the struct, which /1 had. Every consumer is in tree (the nodes, the tests' rigs),
// but a node built against /1 would read a struct a member short, so the id moved.
#define RP_ENGINE_HOST_IO_INTERFACE "rp.engine.host-io/2"
struct EngineHostIo {
    void* ctx;
    // Push the host MIDI staged since the last take onto `out`, stable-sorted by frame, and clear the staging.
    void (*takeMidi)(void* ctx, rp_queue* out);
    // The same for the control-surface MIDI, every message at frame 0.
    void (*takeControllerMidi)(void* ctx, rp_queue* out);
    // Whether a control surface is attached this block.
    bool (*controllerConnected)(void* ctx);
    // Append one message to the host MIDI out (Engine::midiOut), as `system` emitted it. Allocates (see above).
    void (*midiOut)(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size);
    // Append one message to the control-surface out (Engine::controllerOut). Allocates (see above).
    void (*controllerOut)(void* ctx, const std::uint8_t* data, std::size_t size);
    // Hand one message to the physical N8 the engine feeds (Engine::setCoreByteSink); nothing while none is set.
    void (*cartBytes)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush);
};

// The engine's sync trace (HostSyncTrace): a system node records the bytes its `bytes` input feeds the core's byte
// device, so across systems a block's bytes are in plan order, one system's after another's, where the kernel
// stage's were in its emission order. The trace is a diagnostic, and nothing reads that order. Nothing while the
// trace is off. [audio]
#define RP_ENGINE_SYNC_TRACE_INTERFACE "rp.engine.sync-trace/1"
struct EngineSyncTrace {
    void* ctx;
    void (*bytes)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size);
};

}  // namespace rp::host
