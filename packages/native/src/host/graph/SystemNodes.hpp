#pragma once

#include "graph/rp_node.h"
#include "system/SystemTypes.hpp"

namespace rp::host {

// The system node packages (docs/modular-graph-phase-b1-design.md section 3.1): rp.sameboy.gb, and
// rp.mesen.nes / .gba / .sms. Each node adapts one of today's SystemBase cores. A node whose settings name a `system`
// builds it at activation through rp.host.systems/1 (docs/modular-graph-phase-c3-design.md); one that names a parked
// `core` receives it through rp.host.cores/1, which C3's last commit deletes.
const rp_package* sameboyPackage();
const rp_package* mesenPackage();

// The node type a core of this kind is placed as (rp.sameboy.gb, rp.mesen.nes, ...).
const char* systemNodeType(SystemKind kind);

}  // namespace rp::host
