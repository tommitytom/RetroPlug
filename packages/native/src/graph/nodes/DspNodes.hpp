#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.dsp package: the DSP library as graph nodes (docs/modular-graph-phase-d1-design.md section 5), each one a
// thin node around a block-streaming kernel of src/dsp/, so a patch and the kit compilers run the same arithmetic.
//
//   rp.dsp.gain     a linear gain                                              {"channels":2}
//   rp.dsp.biquad   the RBJ cookbook's eight filters, one per channel          {"channels":2,"type":"low-pass"}
//   rp.dsp.dither   quantise to 1..16 bits, five dither algorithms or none     {"channels":2,"bits":4,"seed":...}
//
// Every one carries a `channels` setting and follows it through adjust_layout, as rp.core.delay does: the two
// per-channel types keep their kernel state in host-supplied memory, and gain, whose kernel is one float and has no
// per-channel state, holds it as an ordinary member. None declares `heap` or `threads`, so all three plan on the Pico.
// A kernel's state is per channel and its size cannot be known from the descriptor alone, so extra_memory is sized
// from the settings' channel count, never from the descriptor's maximum.
//
// The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain (heap, pthreads and RTTI) with
// it, and a node that declares `heap` is refused on the Pico. So it is a source file and a target of its own, and a
// build without it omits that node and r8brain together: it registers from a package of its own, with its own
// manifest id (`rp.dsp.resample`, which `rp.host.io` is to `rp.host`), added to the engine's package list and to the
// graph rig's beside dspPackage(). A package id registers once - NodeRegistry refuses a second package under one - so
// the resample type can neither be a second rp.dsp nor a type this function's list names.
const rp_package* dspPackage();

}  // namespace rp::graph
