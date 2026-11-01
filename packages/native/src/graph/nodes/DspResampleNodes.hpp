#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.dsp.resample package (docs/modular-graph-phase-d1-design.md section 5): one type, the resampler, a package
// of its own beside rp.dsp (DspNodes.hpp says why it cannot be one of that package's types).
//
//   rp.dsp.resample   an audio buffer played at the plan's rate   {"buffer":"res:N","channels":2,"layout":"stereo"}
//
// Every port in a plan carries the same frame count at the plan's one rate, so a resampler cannot sit between two
// audio ports. It is a rate-aware sibling of rp.core.buffer-player instead: it reads an "audio-buffer" resource at
// that buffer's own sample rate and outputs at the plan's, pulling input through the resample kernel
// (src/dsp/Resample.hpp) as its output needs it. The kernel feeds r8brain fixed 1024-frame chunks, so the node's output
// stream is the kernel's, the same at any block size.
//
// The buffer is a settings ref rather than a resource port, because the resamplers are built in activate and building
// one allocates; DspResampleNodes.cpp has the threading argument. What that buys costs ordering, and the cost is not
// covered: a recorder (rp.core.buffer-recorder) and a resampler on the same buffer in one plan are unsupported. The
// planner orders a port's reader after its writer and orders nothing for a settings ref, so the node resamples
// whichever prefix of the buffer the block schedule has written when it runs: a silently truncated stream, with no
// diagnostic. D2's driver fills a buffer before the plan that plays it, never in the same plan as the resampler.
//
// This package's id and its only type's id are the same string, "rp.dsp.resample": legal, since the registry keeps
// package ids and type ids apart, but the two lists are not one list.
//
// It declares `heap`, so the planner refuses it on the Pico. Its kernel brings r8brain, pthreads and RTTI with it, so
// it is a target of its own too (retroplug-dsp-resample-nodes, linking retroplug-dsp-resample): a build for a target
// that cannot take those - the Pico's - links neither target and does not name this package in its registration list,
// so the node and r8brain are both absent from its binary, not only refused by its planner.
const rp_package* dspResamplePackage();

}  // namespace rp::graph
