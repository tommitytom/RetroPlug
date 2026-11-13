#pragma once

#include "system/SystemBuild.hpp"

// The blastem package's build function (docs/megadrive-blastem-design.md section 5), which rp.blastem.md's request
// carries to rp.host.systems/2, so the host that builds the core names no console. It reads its own config out of the
// request, which nothing else does: the ROM gate, then the boot (reserve the plan's block, onActivate, refuse a core
// that never came up, enable the state snapshot), with a refusal's reason in *why when it is given.
namespace rp::blastem {

std::unique_ptr<SystemBase> buildMdCore(SystemId id, SystemBuildRequest&& request, double sampleRate, std::string* why);

}  // namespace rp::blastem
