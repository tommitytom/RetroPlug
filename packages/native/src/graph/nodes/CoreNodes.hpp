#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.core package: the explicit combining and conversion nodes the wiring rules require
// (docs/modular-graph-design.md section 3), plus a constant source and buffer playback / recording.
//
//   rp.core.add            "+": sum N audio inputs (or N control inputs)       {"inputs":2,"kind":"audio"}
//   rp.core.merge          merge N events inputs by frame                      {"inputs":2}
//   rp.core.join           concatenate inputs into one port                    {"inputs":[1,1],"layout":"stereo"}
//   rp.core.split          break one port into several                         {"outputs":[1,1],"layout":"stereo"}
//   rp.core.downmix        layout-aware sum                                    {"from":"stereo","to":"mono"}
//   rp.core.relabel        assert a layout over the same channels              {"channels":6,"layout":"5.1"}
//   rp.core.delay          per-channel sample delay                            {"maxSamples":48000,"channels":2}
//   rp.core.constant       a param as a control output                         {"min":0,"max":1}
//   rp.core.buffer-player  play an audio-buffer resource                       {"channels":2,"layout":"stereo","loop":true}
//   rp.core.buffer-recorder write audio into an audio-buffer resource          {"channels":2}
const rp_package* corePackage();

}  // namespace rp::graph
