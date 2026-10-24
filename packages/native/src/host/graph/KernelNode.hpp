#pragma once

#include "graph/rp_node.h"

namespace rp::host {

// rp.dsp.kernel (docs/modular-graph-phase-b1-design.md section 3.3): today's DSP kernel stage as one node,
// first in every plan. It has no ports: it runs the engine's kernel stage through the engine's
// rp.engine.kernel/1 service, which reads the staged MIDI and hands the kernel's output to the cores as
// today. B1 only; B3 replaces it with the protocol nodes.
const rp_package* dspPackage();

}  // namespace rp::host
