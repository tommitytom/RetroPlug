#pragma once

// The engine services the engine's nodes ask the host for through rp_host.get_interface.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"

class SystemBase;

namespace rp::host {

// The core handoff (docs/modular-graph-phase-b2-design.md section 2.5). buildCore parks each built core under
// its system id, which the system node's settings name as {"core": <id>}; the node takes it when first
// activated (not when created) and owns it from then on. [control]
//
// A core belongs to one node for life: take hands a core out once, so a second node naming the same id
// finds nothing and fails to activate. A node that gives its core up (destroyed once no plan runs it, or
// unwound with a plan refused after it took the core, or handed the wrong kind of core) says so through
// `released`, and frees the core unless the host took it back.
#define RP_HOST_CORES_INTERFACE "rp.host.cores/1"
struct HostCores {
    void* ctx;
    // The core parked under `token`, now the caller's; null if none is (taken already, or never parked).
    SystemBase* (*take)(void* ctx, std::uint64_t token);
    // A system node is giving up its core: release what the engine keeps for it (its snapshot slot), or take
    // the core back, parked again for a later document's node to take. True when the host took it back: the
    // node must not delete it.
    bool (*released)(void* ctx, SystemBase* core);
};

// The engine's host I/O (docs/modular-graph-phase-b3a-design.md section 3.3), for the rp.host I/O nodes and the cart
// mirror: the hosts stage what they receive through the engine and drain what it sends after each block, and these
// move it between that staging and the plan's queues. [audio], though not allocation-free: midiOut and controllerOut
// each append one std::vector per message to the buffers the hosts drain, as the kernel stage's sinks did. A flat
// arena, as the staged host MIDI's is (Engine::Staging), is the fix.
#define RP_ENGINE_HOST_IO_INTERFACE "rp.engine.host-io/1"
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
    // Count one message a node was handed and could not deliver (Engine::droppedMessages counts it with the plan's
    // full queues): one on rp.host.midi-out's prefixed input too short to carry the system id that names its sender.
    void (*dropped)(void* ctx);
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
