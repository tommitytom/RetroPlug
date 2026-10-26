#pragma once

#include "graph/rp_node.h"

namespace rp::host {

// rp.dsp.kernel (docs/modular-graph-phase-b1-design.md section 3.3): the DSP kernel stage as one node. It has no
// ports: it runs the engine's kernel stage through the engine's rp.engine.kernel/1 service, which reads the staged
// MIDI and hands the kernel's output to the cores. The Engine registers it no more: the kernel is the script node
// of that id (src/script/kernelNode.ts, docs/modular-graph-phase-b3a-design.md section 4), wired by every document
// TS builds, and this goes with the kernel stage.
const rp_package* dspPackage();

}  // namespace rp::host
