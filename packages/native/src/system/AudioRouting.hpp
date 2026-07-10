#pragma once

#include <cstdint>

// How per-system audio fans out across the 8 plugin output channels. The
// plugin always declares 8 outputs (DISTRHO_PLUGIN_NUM_OUTPUTS); modes
// pick which of those get written, the rest are zero.
//   Stereo          all systems sum into outs 0/1; outs 2..7 silent.
//   TwoPerInstance  system i writes to outs (2i % 8)/(2i % 8 + 1).
//   OnePerInstance  system i writes a mono mix to out (i % 8).
//
// The routing value is owned by TS and pushed into Engine::audioRouting_ via
// the SetAudioRouting RPC; the block runner's MultiOutRouter switches on it.
enum class AudioRouting : std::uint8_t {
    Stereo         = 0,
    TwoPerInstance = 1,
    OnePerInstance = 2,
};
