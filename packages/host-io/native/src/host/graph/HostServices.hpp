#pragma once

// The engine service the host I/O nodes ask the engine for through rp_host.get_interface, in the package that owns those
// nodes (docs/modular-graph-phase-f2-design.md section 2). The system services (rp.host.systems/2 and
// rp.engine.sync-trace/1) are the system package's (system/HostSystems.hpp), since the contract they serve is that
// package's; the device links (rp.host.devices/2) and the job reports (rp.host.jobs/1) a device's node takes, and the
// staged message (rp.stage/1) it presents, are the node SDK's (graph/rp_device.h, graph/rp_jobs.h, graph/rp_stage.h),
// since such a node includes nothing here. The engine implements this one; nothing else of the shell's does.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"  // rp_queue

namespace rp::host {

// The engine's host I/O (docs/modular-graph-phase-b3a-design.md section 3.3), for the rp.host I/O nodes: the hosts
// stage what they receive through the engine and drain what it sends after each block, and these move it between that
// staging and the plan's queues. [audio], though not allocation-free: midiOut appends one std::vector per message to
// the buffer the hosts drain, as the native kernel stage's sink did before B3a. A flat arena, as the staged host
// MIDI's is (Engine::Staging), is the fix.
//
// Host services are looked up by this versioned id so that a change of shape is visible to a node built against the
// old one. /2: the drop counter left the struct, which /1 had. /3: the control-surface half (takeControllerMidi,
// controllerConnected, controllerOut) and cartBytes left it, which /2 had, with the nodes that read them: the
// rp.launchpad.surface-in / -out nodes take the device's traffic from their link, and rp.n8.nes pushes at its cart's.
// Every consumer is in tree (the nodes, the tests' rigs), but a node built against /2 would read a struct a member
// past its end, so the id moved.
#define RP_ENGINE_HOST_IO_INTERFACE "rp.engine.host-io/3"
struct EngineHostIo {
    void* ctx;
    // Push the host MIDI staged since the last take onto `out`, stable-sorted by frame, and clear the staging.
    void (*takeMidi)(void* ctx, rp_queue* out);
    // Append one message to the host MIDI out (Engine::midiOut), as `system` emitted it. Allocates (see above).
    void (*midiOut)(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size);
};

}  // namespace rp::host
