#pragma once

#include "graph/rp_node.h"

namespace rp::device_io {

// The rp.device package: a device host's own I/O as node types, the twins of the engine's rp.host nodes for a host that
// is firmware (docs/modular-graph-phase-e3-design.md section 5).
//
//   rp.device.midi-in   the MIDI the host parsed and staged for the block (rp.device.midi-in/1,
//                       graph/rp_device_midi_in.h), on a `midi` events output of 256 messages and 4 KB, the box's UART
//                       ring's size. A plan holds one (`unique`): the host's staging is one stream, which two nodes
//                       would split between them. A host that offers no rp.device.midi-in/1 (a desktop's) gives it
//                       nothing, nor does the exporter's, which offers one with nothing staged, so its output is
//                       empty; it activates in every host.
const rp_package* deviceIoPackage();

}  // namespace rp::device_io

// The package's entry, named in packages/device-io/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_device_io_package(void);
