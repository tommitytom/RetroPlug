#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.core package: the explicit combining and conversion nodes the wiring rules require
// (docs/modular-graph-design.md section 3), plus a constant source and buffer playback / recording.
//
//   rp.core.add             "+": sum N audio inputs (or N control inputs)  {"inputs":2,"kind":"audio"}
//   rp.core.merge           merge N events inputs by frame                 {"inputs":2}
//   rp.core.join            concatenate inputs into one port               {"inputs":[1,1],"layout":"stereo"}
//   rp.core.split           break one port into several                    {"outputs":[1,1],"layout":"stereo"}
//   rp.core.downmix         layout-aware sum                               {"from":"stereo","to":"mono"}
//   rp.core.relabel         assert a layout over the same channels         {"channels":6,"layout":"5.1"}
//   rp.core.delay           per-channel sample delay                       {"maxSamples":48000,"channels":2}
//   rp.core.constant        a param as a control output                    {"min":0,"max":1}
//   rp.core.buffer-player   play an audio-buffer resource                  {"channels":2,"layout":"stereo","loop":true}
//   rp.core.buffer-recorder write audio into an audio-buffer resource      {"channels":2}
//
// The player starts a buffer from its first frame whenever it is handed a different one, ignores the
// buffer's sample rate (it plays frames, not seconds), and leaves output channels the buffer lacks silent.
// The recorder's Record param is frame-accurate: turning it on, or binding a new target, starts again
// from the buffer's first frame, and recording stops at its last; buffer channels beyond its input keep
// whatever they held. Both spot a new buffer by its handle's address, so one freed and another made at
// the same address while the node was unbound reads as the same buffer (an offline driver can arrange
// that; a running host cannot, since the old plan pins the old buffer until the new one exists).
const rp_package* corePackage();

}  // namespace rp::graph
