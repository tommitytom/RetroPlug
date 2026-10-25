#pragma once

// The engine services the engine's nodes ask the host for through rp_host.get_interface. B3 dissolves the
// kernel, and with it rp.engine.kernel/1.

#include <cstdint>

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
    // the core back (parked again under its token). True when the host took it back: the node must not
    // delete it.
    bool (*released)(void* ctx, SystemBase* core);
};

// The engine's kernel stage (section 3.3), run by rp.dsp.kernel as the plan's first step. [audio]
#define RP_ENGINE_KERNEL_INTERFACE "rp.engine.kernel/1"
struct EngineKernel {
    void* ctx;
    void (*run)(void* ctx, std::uint32_t frames);
};

}  // namespace rp::host
