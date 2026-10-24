#pragma once

// The engine services B1's nodes ask the host for through rp_host.get_interface. B1 only: B2 hands
// documents to TS and B3 dissolves the kernel, and each removes the service it no longer needs.

#include <cstdint>

class SystemBase;

namespace rp::host {

// The core handoff (docs/modular-graph-phase-b1-design.md section 4.6). The translator parks each built
// core under a token that goes in the system node's settings; the node takes it when first activated (not
// when created) and owns it from then on. [control]
//
// A core belongs to one node id for life: take hands a token out once, so the same token under a new node
// id finds nothing and that node fails to activate. A plan refused after its node took a core (a later node
// failed, or the token named the wrong kind of core) has already released and freed that core.
#define RP_HOST_CORES_INTERFACE "rp.host.cores/1"
struct HostCores {
    void* ctx;
    // The core parked under `token`, now the caller's; null if none is (taken already, or never parked).
    SystemBase* (*take)(void* ctx, std::uint64_t token);
    // A system node is destroying its core: release what the engine keeps for it (its snapshot slot).
    void (*released)(void* ctx, SystemBase* core);
};

// The engine's kernel stage (section 3.3), run by rp.dsp.kernel as the plan's first step. [audio]
#define RP_ENGINE_KERNEL_INTERFACE "rp.engine.kernel/1"
struct EngineKernel {
    void* ctx;
    void (*run)(void* ctx, std::uint32_t frames);
};

}  // namespace rp::host
