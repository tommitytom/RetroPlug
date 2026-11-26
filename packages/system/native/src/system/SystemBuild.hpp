#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#include "graph/rp_node.h"         // rp_error
#include "system/SystemTypes.hpp"  // SystemId

class SystemBase;

struct SystemBuildRequest;

// A system to build (docs/modular-graph-phase-c3-design.md section 5): the bytes a system node's resources hold, the
// ROM's name, and the console config its settings make. Every core is built this way, so there is one boot path per
// console, and the node whose package owns the console carries the function that reads it
// (docs/modular-graph-phase-f2-design.md section 3): the host builds what the request names, and knows no console.
//
// [control]: the build runs inside the setGraph that activates the node that asked for it, so `config` points at a
// config the node holds for the call and `build` runs synchronously. A build function is a plain function pointer, with
// no capture, because the node's type names it. The request is views of what the node holds for the call: the build
// copies what its core keeps, since the node may be a pack's, with a heap of its own
// (docs/downloadable-packs-m5a-plan.md: no heap block crosses a pack's boundary).

// `id` is the system the core is built under, `request` what to build it from (its `config` is the console's, which
// only this function reads), `sampleRate` the rate the plan runs at, and `why` receives a refusal's reason
// (refuseBuild). Returns an already-onActivate'd core, or null: for a ROM the console's gate refuses, a boot the core refuses, or a request that
// carried no config (the node passes the one its settings made, so a config of the wrong console's is a bug no build
// function can see through the void pointer). The core is the caller's, deleted through SystemBase's virtual
// destructor, whose deleting form is this module's: the one object that crosses the boundary, with its deleter.
using SystemBuildFn = std::unique_ptr<SystemBase> (*)(SystemId id, const SystemBuildRequest& request,
                                                      double sampleRate, rp_error* why);

struct SystemBuildRequest {
    // Views of the bytes the node's resources pin, alive for the call.
    std::span<const std::uint8_t> rom;
    std::span<const std::uint8_t> sram;       // battery RAM seed (may be empty)
    std::span<const std::uint8_t> savestate;  // savestate seed (may be empty)
    // The ROM's file stem (a view of the node's setting). A Master System or Game Gear core stages its ROM as a file and takes its battery stem from
    // that name, and now that its ROM is bytes it has nothing else to name the file by; empty keeps the core's "rom".
    std::string_view romName;
    // The largest block the plan this core is built for will run, in frames; 0 for a build outside a plan (a test, a
    // tool), which then keeps the console's default. It is reserved before the core activates, so its audio ring and
    // its per-block scratches are sized to the plan from the start rather than to the default and then grown
    // (docs/modular-graph-phase-c4-design.md section 4). A host that grows its block later reserves again, through
    // SystemBase::reserveAudio on the reactivation that is quiescent with respect to the core.
    std::uint32_t maxBlockFrames = 0;
    // The node's package's build function, and the console config its settings made, which only that function reads (a
    // SameBoyConfig, a MesenNesConfig): the node holds the config for the duration of the call. `build` null is a
    // request that names no console, which the host refuses before it claims anything.
    SystemBuildFn build  = nullptr;
    const void*   config = nullptr;
    // The node type that made the request (its `id`), which the host names in one refusal: a type that builds a core but
    // whose descriptor does not declare category "system" has no slot in the plan's table, and that refusal names the
    // type whose descriptor to fix. A node that names none reads as no type in that message.
    const char* nodeType = nullptr;
};

// A build function's refusal: `reason` copied into `why` (cut to its buffer), before the function returns null.
inline void refuseBuild(rp_error* why, std::string_view reason) {
    if (why == nullptr) return;
    const std::size_t n = std::min(reason.size(), sizeof why->message - 1);
    std::memcpy(why->message, reason.data(), n);
    why->message[n] = '\0';
}
