#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The node types a GraphHost implements itself (section 5.4). Phase A ships rp.host.audio-out, which
// copies its inputs into the host buffers passed to GraphHost::processBlock. Its settings list the host's
// buses ({"buses":[2,2,2,2]} for the plugin's four stereo pairs); each becomes one port named after the
// channels it lands on: "out.1-2", "out.3-4", or "out.5" for a mono bus.
const rp_package* hostPackage();

}  // namespace rp::graph

// The package's entry, named in packages/graph-runtime/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_host_package(void);
