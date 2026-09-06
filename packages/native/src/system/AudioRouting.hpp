#pragma once

#include <cstdint>

// How per-system audio fans out across the 8 plugin output channels. The
// plugin always declares 8 outputs (DISTRHO_PLUGIN_NUM_OUTPUTS); modes
// pick which of those get written, the rest are zero.
//   Stereo          all systems sum into outs 0/1; outs 2..7 silent.
//   TwoPerInstance  system i writes to outs (2i % 8)/(2i % 8 + 1).
//   OnePerInstance  system i writes a mono mix to out (i % 8).
//   ChannelSplit    ONE system fans its per-channel streams across the outputs
//                   (a Game Boy's 4 STEREO channels -> outs 0/1,2/3,4/5,6/7; a
//                   NES's 5 MONO core channels -> outs 0..4).
//   PinSplit        ONE NES fans its three 2A03 output-pin streams (Pulse | TND
//                   | lumped Expansion) across outs 0..2, mono. NES-only — any
//                   other single system falls back to Stereo.
//   StereoPinSplit  the same three pins, but each on its OWN stereo PAIR (outs
//                   0/1, 2/3, 4/5) with the mono stem mirrored into both lanes.
//                   Six lanes instead of three, and the reason to spend them is
//                   the DAW: every pin arrives as a normal centred stereo track,
//                   so hosts that can only bus/FX whole stereo pairs can treat
//                   each pin separately. Under PinSplit the pins share pairs
//                   (Pulse on L of track 1, TND on its R), which those hosts
//                   cannot pull apart. NES-only, like PinSplit.
//
// All three split modes are single-system only; the Engine gates them
// (systemCount()==1) and falls back to Stereo for any other project.
//
// Modes 0..2 fan MANY systems across the fixed pairs (MultiOutRouter); modes 3..5
// split ONE system's channels (ChannelSplitRouter, whose laneStride is 2 for a
// pair per stream and 1 for a lane per stream). The routing value is owned by TS
// and pushed into Engine::audioRouting_ via the SetAudioRouting RPC; the Engine
// resolves the lane plan on every routing/system change (Engine::syncSplitPlan)
// and picks the router from it in processBlock().
enum class AudioRouting : std::uint8_t {
    Stereo         = 0,
    TwoPerInstance = 1,
    OnePerInstance = 2,
    ChannelSplit   = 3,
    PinSplit       = 4,
    StereoPinSplit = 5,
};
