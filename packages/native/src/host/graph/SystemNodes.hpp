#pragma once

#include "graph/rp_node.h"

namespace rp::host {

// The system node packages (docs/modular-graph-phase-b1-design.md section 3.1): rp.sameboy.gb, and
// rp.mesen.nes / .gba / .sms. Each node adapts one of today's SystemBase cores, and builds its own at activation from
// the ROM and seed resources its settings name, through rp.host.systems/1 (docs/modular-graph-phase-c3-design.md).
const rp_package* sameboyPackage();
const rp_package* mesenPackage();

// The NES's `expansionVolume` param (docs/modular-graph-phase-c2-design.md section 3): its cartridge sound chip's level
// in whole percent of unity. One range for the emulated NES, which hands it to its core, and for rp.n8.nes, which
// rescales it onto the real cart's master-volume register, so the same document value sounds the same on both.
inline constexpr int kExpansionVolumeMin = 0;
inline constexpr int kExpansionVolumeMax = 200;
inline constexpr int kExpansionVolumeDefault = 100;

}  // namespace rp::host

// The packages' entries, named in packages/native/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_sameboy_package(void);
extern "C" const rp_package* rp_mesen_package(void);
