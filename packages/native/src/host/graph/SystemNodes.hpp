#pragma once

#include "graph/rp_node.h"

namespace rp::host {

// The system node packages (docs/modular-graph-phase-b1-design.md section 3.1): rp.sameboy.gb, and
// rp.mesen.nes / .gba / .sms. Each node adapts one of today's SystemBase cores; it needs the engine's
// rp.host.cores/1 service to receive its core.
const rp_package* sameboyPackage();
const rp_package* mesenPackage();

}  // namespace rp::host
