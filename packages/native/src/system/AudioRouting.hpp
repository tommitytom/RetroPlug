#pragma once

#include <cstdint>

// How per-system audio fans out across the 8 plugin output channels. The
// plugin always declares 8 outputs (DISTRHO_PLUGIN_NUM_OUTPUTS); modes
// pick which of those get written, the rest are zero.
//   Stereo          all systems sum into outs 0/1; outs 2..7 silent.
//   TwoPerInstance  system i writes to outs (2i % 8)/(2i % 8 + 1).
//   OnePerInstance  system i writes a mono mix to out (i % 8).
//   ChannelSplit    ONE system fans its per-channel streams across the output
//                   pairs (a Game Boy's 4 channels -> outs 0/1,2/3,4/5,6/7).
//                   Single-system only; the Engine gates it (systemCount()==1)
//                   and falls back to Stereo for any other project.
//
// Modes 0..2 fan MANY systems across the fixed pairs (MultiOutRouter); mode 3
// splits ONE system's channels (ChannelSplitRouter). The routing value is owned
// by TS and pushed into Engine::audioRouting_ via the SetAudioRouting RPC; the
// Engine picks the router per mode + system count in processBlock().
enum class AudioRouting : std::uint8_t {
    Stereo         = 0,
    TwoPerInstance = 1,
    OnePerInstance = 2,
    ChannelSplit   = 3,
};
