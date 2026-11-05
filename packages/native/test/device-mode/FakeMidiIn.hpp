#pragma once

// A device host's MIDI input for the desktop's tests (rp.device.midi-in/1, graph/rp_device_midi_in.h): what a test
// stages before a block, handed out one message a call in staging order, and none again once it is all taken, as the
// box's host stages from its UART. Desktop-built: it allocates and frees as a test stages (a stage after a block that
// took everything starts the staging afresh), never in next.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "graph/rp_device_midi_in.h"

namespace rp::graph::devtest {

struct FakeMidiIn {
    struct Staged {
        std::uint32_t             frame;
        std::vector<std::uint8_t> bytes;
        std::uint32_t             claimed = 0;  // a size to answer in place of the bytes' own (a host's bug), or 0
    };
    std::vector<Staged> staged;
    std::size_t         taken = 0;
    std::uint32_t       calls = 0;       // every call to next
    std::uint32_t       lastFrames = 0;  // the block length the last call named
    rp_device_midi_in   iface{this, &next};

    void stage(std::uint32_t frame, std::vector<std::uint8_t> bytes) {
        if (taken == staged.size()) {
            staged.clear();
            taken = 0;
        }
        staged.push_back({frame, std::move(bytes), 0});
    }
    // A message whose answered size is `size`, whatever its bytes hold: what a broken host hands the node.
    void stageClaiming(std::uint32_t frame, std::vector<std::uint8_t> bytes, std::uint32_t size) {
        stage(frame, std::move(bytes));
        staged.back().claimed = size;
    }

    static bool next(void* ctx, std::uint32_t frames, rp_device_midi_message* out) {
        auto* self = static_cast<FakeMidiIn*>(ctx);
        ++self->calls;
        self->lastFrames = frames;
        if (self->taken == self->staged.size()) return false;
        const Staged& s = self->staged[self->taken++];
        const auto size = s.claimed ? s.claimed : static_cast<std::uint32_t>(s.bytes.size());
        *out = rp_device_midi_message{s.frame, size, s.bytes.data()};
        return true;
    }
};

}  // namespace rp::graph::devtest
