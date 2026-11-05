#pragma once

// A box's rp_host (docs/modular-graph-phase-e3-design.md section 5): what the nodes of its plan are created under. It
// offers two interfaces and nothing else: rp.host.devices/2 over a small table of links the product fills (the N8 box's
// cart link, installed as "n8" with rp.n8.cart-link/1), and rp.device.midi-in/1, the staged MIDI (MidiStaging). No
// heap (alloc and free are null, as a target with none has them), no rp.host.jobs/1 (a node that would run a device job
// is told "this host takes no device jobs"), no resources.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does.

#include <cstddef>
#include <cstdint>

#include "graph/rp_device.h"
#include "graph/rp_device_midi_in.h"
#include "graph/rp_node.h"

namespace rp::box {

// One link a host installs: the name and the interface id a node acquires it by (RP_DEVICE_NAME_MAX and
// RP_DEVICE_INTERFACE_ID_MAX bound them), the link as that interface's pointer, and the link's own hooks, told of each
// acquire and release (a cart link counts the nodes holding it). Either hook may be null.
struct DeviceLink {
    const char* name;
    const char* interfaceId;
    void*       link;
    void*       hookCtx;
    void (*acquired)(void* ctx);
    void (*released)(void* ctx);
};

class DeviceHost {
public:
    // A line the host's nodes log, with its RP_LOG_* level.
    using Log = void (*)(int level, const char* message);

    constexpr DeviceHost() = default;
    // The links are the caller's and outlive every plan created under this host.
    void init(const DeviceLink* links, std::size_t count, const rp_device_midi_in* midi, Log log);

    const rp_host* host() const { return &host_; }

private:
    static void*       acquireFn(void* ctx, const char* name, const char* interfaceId);
    static void        releaseFn(void* ctx, void* link);
    static void        logFn(void* ctx, int level, const char* message);
    static const void* interfaceFn(const rp_host* host, const char* id);

    const DeviceLink*        links_ = nullptr;
    std::size_t              count_ = 0;
    const rp_device_midi_in* midi_ = nullptr;
    Log                      log_ = nullptr;
    rp_host_devices          devices_{this, &acquireFn, &releaseFn};
    rp_host                  host_{RP_ABI_VERSION, this, nullptr, nullptr, &logFn, &interfaceFn};
};

}  // namespace rp::box
