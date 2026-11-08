#pragma once

#include "system/SystemBuild.hpp"

// The Game Boy's build function (docs/modular-graph-phase-f2-design.md section 3): what rp.sameboy.gb's request carries
// and rp.host.systems/2 runs, so the host that builds the core names no console. It reads a SameBoyConfig from the
// request's `config`, which only it does: the node that made the settings passed it, and the host moved it here opaque.
//
// SameBoy-only: a ROM that is another console's, or nothing at all, is rejected (nullptr, with the reason in *why), the
// embedded mGB included (it carries a cart's logo).
namespace rp::sameboy {

// The ROM gate, the seeds into the config BEFORE construct (so a live core restores them inside onActivate, the
// savestate last), onActivate, then enableStateSnapshot. The config and the request's bytes are consumed.
std::unique_ptr<SystemBase> buildCore(SystemId id, SystemBuildRequest&& request, double sampleRate, std::string* why);

}  // namespace rp::sameboy
