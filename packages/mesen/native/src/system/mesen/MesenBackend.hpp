#pragma once

#include "system/SystemBuild.hpp"

// The Mesen core's build functions (docs/modular-graph-phase-f2-design.md section 3), one per console it runs: NES,
// GBA, and Master System / Game Gear. A node's request carries the one its own type names, and rp.host.systems/3 runs
// it, so the host that builds the core names no console. Each reads its own config from the request's `config`, which
// nothing else does. GBA runs on Mesen's HLE boot ROM (biosPath left empty).
namespace rp::mesen {

// Each is a ROM gate, then bootMesen (construct, reserve the plan's block, onActivate, reject a refused ROM, enable the
// state snapshot). The config and the request's bytes are moved into the core; a refusal's reason is in *why when it is
// given.
std::unique_ptr<SystemBase> buildNesCore(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                         rp_error* why);
std::unique_ptr<SystemBase> buildGbaCore(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                         rp_error* why);
std::unique_ptr<SystemBase> buildSmsCore(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                         rp_error* why);

}  // namespace rp::mesen
