#pragma once

#include "host/packs/PackKinds.hpp"

namespace rp::host::packs {

// The trial every host's table runs a kinds half through (docs/downloadable-packs-m2f-plan.md): the bundle compiled to
// global-code bytecode, then loaded into a scratch script host as an Engine's construction loads it: after the
// product's built-in script nodes and every earlier pack's kinds half, so a chunk that throws (a kind the product does
// not declare, an id a built-in package or an earlier pack holds, a bad module) is refused before any Engine meets it.
// A recording wrapper around the registrar then holds the chunk to its manifest: it registers exactly the ids its
// pack.json's kinds member declares. The scratch host is the call's alone, freed before it returns.
KindsTrial kindsTrial();

}  // namespace rp::host::packs
