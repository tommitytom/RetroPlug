#pragma once

#include "host/packs/PackProtocol.hpp"

namespace rp::host::packs {

// The trial every host's table runs a protocol half through (docs/downloadable-packs-m2e-plan.md): the bundle compiled
// to global-code bytecode, then loaded into a scratch script host after the product's built-in script nodes and both
// registered on a scratch registry, so a type the built-ins hold, a descriptor the planner cannot use or a bundle that
// throws is refused before any Engine meets it. The scratch host is the call's alone, freed before it returns.
ProtocolTrial protocolTrial();

}  // namespace rp::host::packs
