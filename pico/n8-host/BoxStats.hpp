#pragma once

// The box's `filtered` count (main.cpp's [stats]), from the plan's two edges, which the host owns: what the staging
// dropped before the plan (a SysEx past 4 KB, staged messages no block took) and what the plan took and did not hand
// the cart link from its input (what its filter dropped, and rp.device.midi-in's own overflow). One function, so the
// desktop's test holds the firmware's arithmetic rather than a copy of it.
//
// Exact but in one case, which only a full link ring (`linkfull=` moving) reaches: when rp.n8.nes's payment of an
// owed note-off is refused for room and the block's input carries the note-off for that note, the link counts the
// input's push as the node's own (`made`), so `filtered` reads one high.

#include <cstdint>

#include "BoxCartLink.hpp"
#include "MidiStaging.hpp"

namespace rp::box {

inline std::uint32_t filtered(const MidiStaging::Counts& in, const BoxCartLink::Counts& out) {
    const std::int64_t dropped = std::int64_t{in.handed} + in.tooLong + in.untaken - out.fromPlanInput();
    return dropped > 0 ? static_cast<std::uint32_t>(dropped) : 0;
}

}  // namespace rp::box
