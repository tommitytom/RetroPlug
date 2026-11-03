#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.dsp package: the DSP library as graph nodes (docs/modular-graph-phase-d1-design.md section 5), each one a
// thin node around the kernels of src/dsp/, so a patch and the kit compilers run the same arithmetic. rp.dsp.normalize
// is the one that is not: a buffer player, whose normalising multiply is the gain kernel's, applied to a scale the
// buffer's own peak sets.
//
//   rp.dsp.gain       a linear gain                                              {"channels":2}
//   rp.dsp.biquad     the RBJ cookbook's eight filters, one per channel          {"channels":2,"type":"low-pass"}
//   rp.dsp.dither     quantise to 1..16 bits, five dither algorithms or none     {"channels":2,"bits":4,"seed":...}
//   rp.dsp.normalize  a buffer player, normalised by its own peak                {"buffer":"res:N","gain":1,"channels":1}
//   rp.dsp.dpcm       risa's DMC encoder, its delta bits as +1/-1 audio          {"buffer":"res:N","normalize":true}
//
// The three processor types carry a `channels` setting and follow it through adjust_layout, as rp.core.delay does: the
// two per-channel types keep their kernel state in host-supplied memory, and gain, whose kernel is one float and has no
// per-channel state, holds it as an ordinary member. A kernel's state is per channel and its size cannot be known from
// the descriptor alone, so extra_memory is sized from the settings' channel count, never from the descriptor's maximum.
//
// The last two name an "audio-buffer" in their settings, as rp.dsp.resample does, because what they need of it must be
// known before a block runs: the normalize node's peak, measured at activate, and the DPCM node's bits, encoded whole
// at activate. Nothing is pinned on the block thread and nothing allocates there either, but the DPCM node's storage is
// built at activate, so it alone declares `heap` and is refused on the Pico. Dither's `seedFrom` is a buffer the same
// way: its hash is taken at activate.
//
// The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain with it - the heap, pthreads
// (r8brain's filter cache is a std::mutex) and RTTI (its resamplers are virtual) - so a build that has no use for it
// must be able to leave it out at link time rather than refuse it at the planner, which is all a `heap` type like
// rp.dsp.dpcm costs the Pico. So it is a source file and a target of its own (DspResampleNodes.hpp,
// retroplug-dsp-resample-nodes), and a build without it omits that node and r8brain together: it registers from a
// package of its own, with its own manifest id (`rp.dsp.resample`, which `rp.host.io` is to `rp.host`), in the
// engine's package list and the graph rig's beside dspPackage(). A package id registers once - NodeRegistry refuses a
// second package under one - so the resample type can neither be a second rp.dsp nor a type this function's list
// names.
const rp_package* dspPackage();

}  // namespace rp::graph

// The package's entry, named in packages/dsp/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_dsp_package(void);
