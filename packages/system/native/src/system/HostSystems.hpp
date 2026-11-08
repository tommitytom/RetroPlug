#pragma once

// The system services a system node asks its host for through rp_host.get_interface
// (docs/modular-graph-phase-c3-design.md section 5, docs/modular-graph-phase-f2-design.md section 3): the builder it
// hands a SystemBuildRequest to, and the engine's sync trace. They live in the system package rather than the shell's
// host/graph/HostServices.hpp because they are what the contract is about - the nodes bind them, the engine implements
// them - and a console's package carries them without reaching the shell. The device links (rp.host.devices/2) and the
// job reports (rp.host.jobs/1) a device's node takes, and the staged message (rp.stage/1) it presents, are the node
// SDK's (graph/rp_device.h, graph/rp_jobs.h, graph/rp_stage.h), and the engine's host I/O (rp.engine.host-io/3) is the
// host-io package's.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"
#include "system/SystemBuild.hpp"

class SystemBase;

namespace rp::host {

#define RP_HOST_SYSTEMS_INTERFACE "rp.host.systems/2"
// A system node building its core where it runs. [control]: build and destroy both run there, build inside the setGraph
// that activates the node and destroy at the reclaim that frees the plan which last ran it (or as a refused plan
// unwinds). A node's resources are pinned by its instance already, so a build reads bytes rather than files.
struct HostSystems {
    void* ctx;
    // Build the core of the system `system`, from the ROM and seeds the node's resources hold, the config its settings
    // made, and the build function its package carries. Null, with the reason in *err, for a request that names no
    // build function, a ROM that console's gate refuses, a boot the core refuses, an id that is live (held by a node of
    // any plan not yet reclaimed), a full snapshot pool, and a build whose node type's descriptor does not declare
    // category "system" (the plan's table has no slot the core would go in, and the reason names the type). A core built
    // here is the node's until `destroy`: the host claimed its snapshot slot under `system` and keeps it among its live
    // cores, so the plan's table and the control plane see it at once.
    //
    // /2: the request carries a build function and an opaque config pointer where /1 carried a SystemConfig variant, so
    // the host no longer picks a backend by the alternative it held. A node built against /1 would read the config as a
    // variant, which is why the id moved; every consumer is in tree (the system nodes, the tests' rigs).
    SystemBase* (*build)(void* ctx, std::uint64_t system, SystemBuildRequest request, rp_error* err);
    // The node is giving up a core it built: release its snapshot slot, leave the live cores and delete it.
    void (*destroy)(void* ctx, SystemBase* core);
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
