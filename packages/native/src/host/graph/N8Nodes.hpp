#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "graph/rp_node.h"

namespace rp::host {

// The rp.n8 package: the real NES an Everdrive N8 Pro cart is plugged into.
//
//   rp.n8.cart-mirror  (docs/modular-graph-phase-b3a-design.md section 3.5), {"system": <id>}: everything on its midi
//                      input, then everything on its bytes input (each with its flush flag), handed to the physical N8
//                      the engine feeds, in queue order. The system it names is the one the cart mirrors, which the
//                      engine reads off the plan: a NES of the document's, one mirror at most.
//   rp.n8.nes          (docs/modular-graph-phase-c5-design.md section 4): the cart as a node of its own, through the
//                      host's cart link (rp.host.devices/1, "n8"). The same `midi` and `bytes` inputs as rp.mesen.nes,
//                      so a system's protocol nodes feed both by fan-out, pushed at the link with their frames, its
//                      MIDI then its bytes; a `connected` control output, 1 while the cart is attached and streaming (0
//                      in a host with no cart, and while a job holds the link); the `expansionVolume` param, rescaled
//                      onto the cart's master-volume register; and the battery file and ROM as capability jobs
//                      (rp.system.sram/1, rp.system.rom/1) run on the link's thread. No audio: the console makes it.
const rp_package* n8Package();

// The system a cart mirror's settings (JSON) name, read as the node reads them, so the engine and the node accept
// the same settings: a field the node does not know is ignored, a missing or out-of-range system refused (nullopt).
std::optional<std::uint32_t> cartMirrorSystem(std::string_view settings);

// rp.n8.nes's `expansionVolume` (whole percent of unity, as the emulated NES's) as the cart's master-volume register:
// 128 is unity, clamped to the register's 0..255. The same rounding as the UI's expVolToN8 (n8Devices.ts), so the
// node and the host's own Settings > N8 row write the same byte for one value.
int expansionVolumeRegister(double percent);

}  // namespace rp::host
