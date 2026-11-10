#pragma once

#include <cstdint>

#include "graph/rp_node.h"
#include "n8wire/ExpansionVolume.hpp"

namespace rp::n8 {

// The rp.n8 package: the real NES an Everdrive N8 Pro cart is plugged into.
//
//   rp.n8.nes          (docs/modular-graph-phase-c5-design.md section 4): the cart as a node of its own, through the
//                      host's cart link (rp.host.devices/2: "n8", rp::n8::CartLink, n8/CartLink.hpp). The same `midi`
//                      and `bytes` inputs as rp.mesen.nes, so a system's protocol nodes feed both by fan-out, pushed at
//                      the link with their frames, its MIDI then its bytes; a `connected` control output, 1 while the
//                      cart is attached and streaming (0 in a host with no cart, and while a job holds the link); the
//                      `expansionVolume` param, rescaled onto the cart's master-volume register; and the battery file
//                      and ROM as capability jobs (rp.system.sram/1, rp.system.rom/1) the link runs. No audio: the
//                      console makes it.
const rp_package* n8Package();

// rp.n8.nes's `expansionVolume` (whole percent of unity, as the emulated NES's) as the cart's master-volume register:
// 128 is unity, clamped to the register's 0..255. The node is the only writer now: buildGraph gives it the system's own
// knob as its param, so the emulated chip and the cartridge's are set from one value. The range it is a percent OF is
// n8wire/ExpansionVolume.hpp's, the one rp.mesen.nes declares its own param from.
int expansionVolumeRegister(double percent);

}  // namespace rp::n8

// The package's entry, named in packages/n8/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_n8_package(void);
