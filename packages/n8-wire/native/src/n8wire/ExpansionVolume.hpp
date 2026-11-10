#pragma once

// The expansion-volume range rp.mesen.nes and rp.n8.nes share (docs/modular-graph-phase-f5a-design.md section 4): whole
// percent of unity, so the emulated NES's chip and a real cart's master-volume register are set from one range and a
// document sounds the same on either. One TS knob drives both nodes, and the two nodes' types are in two packages
// (`mesen` and `n8`), so the range lives where both of them can reach it and neither owns it. Only the cart's side
// rescales it (rp::n8::expansionVolumeRegister, in the n8 pack).
//
// Header-only and heap-free, so every device build compiles it, through rp.n8.nes (N8Node.cpp), as the desktop does.

namespace rp::n8 {

inline constexpr int kExpansionVolumeMin = 0;
inline constexpr int kExpansionVolumeMax = 200;
inline constexpr int kExpansionVolumeDefault = 100;

}  // namespace rp::n8
