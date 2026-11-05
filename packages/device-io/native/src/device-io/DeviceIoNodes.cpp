#include "device-io/DeviceIoNodes.hpp"

#include <array>
#include <cstdint>
#include <span>

#include "graph/node.hpp"
#include "graph/rp_device_midi_in.h"

namespace rp::device_io {

namespace {

using MidiQueue =
    rp::events_out::sized<rp::capacity{.messages = RP_DEVICE_MIDI_IN_MESSAGES, .bytes = RP_DEVICE_MIDI_IN_BYTES}>;

// The host's MIDI input, as rp.host.midi-in is the engine's. Each block it takes every message the host staged for it,
// in the host's order, at the host's frames: one at or past the block's end is clamped to its last frame, and an
// earlier frame than the last taken is clamped up to it by the push, so the queue stays in order and inside the block
// whatever a host stages. What the queue cannot take is dropped and counted as its own, as any full queue drops, and so
// is a message longer than the whole queue's bytes, before its length reaches a push (a length is the host's word, and
// nothing that long can fit); an empty message carries nothing and is not pushed.
struct MidiIn {
    static constexpr const char* id = "rp.device.midi-in";
    static constexpr const char* name = "MIDI In (device)";
    static constexpr const char* category = "device";
    static constexpr bool unique = true;
    struct { MidiQueue midi; } outputs;

    const rp_device_midi_in* in = nullptr;  // null in a host that offers none: nothing to take, never a fault
    void bind_host(const rp_host* h) noexcept {
        in = static_cast<const rp_device_midi_in*>(h->get_interface(h, RP_DEVICE_MIDI_IN_INTERFACE));
    }

    void process(const rp::Block& b) noexcept {
        if (!in) return;
        const std::uint32_t last = b.frames() > 0 ? b.frames() - 1 : 0;
        rp_device_midi_message m{};
        while (in->next(in->ctx, b.frames(), &m)) {
            if (m.size == 0 || m.data == nullptr) continue;
            if (m.size > RP_DEVICE_MIDI_IN_BYTES) {
                ++outputs.midi.queue->dropped;
                continue;
            }
            outputs.midi.push(m.frame < last ? m.frame : last, std::span<const std::uint8_t>(m.data, m.size));
        }
    }
};

}  // namespace

const rp_package* deviceIoPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<MidiIn>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.device","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::device_io

extern "C" const rp_package* rp_device_io_package(void) { return rp::device_io::deviceIoPackage(); }
