#pragma once

// Two nodes for driving and reading the engine's nodes in a NodeRig: a source a test scripts block by block, with
// one output per system node input, and a probe that records what reaches its inputs.

#include <array>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

#include "graph/node.hpp"

namespace rp::host::test {

// Large enough that nothing a test queues is dropped.
inline constexpr rp::capacity kFeedCapacity{.messages = 1024, .bytes = 65536};

struct Message {
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
    std::uint32_t             flags = 0;  // RP_MESSAGE_*
    bool operator==(const Message&) const = default;
};

// Pushes what the test queued in `next` for each output on its next block, in order, then forgets it. The outputs
// are named after the system node inputs they feed.
struct Feed {
    static constexpr const char* id = "rp.test.feed";
    struct {
        rp::bytes_out::sized<kFeedCapacity>  serialIn;
        rp::events_out::sized<kFeedCapacity> midi;
        rp::bytes_out::sized<kFeedCapacity>  bytes;
        rp::events_out::sized<kFeedCapacity> buttons;
    } outputs;
    enum Port : std::uint32_t { SerialIn, Midi, Bytes, Buttons };

    std::array<std::vector<Message>, 4> next;

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t o = 0; o < next.size(); ++o) {
            for (const Message& m : next[o]) rp::graph::pushMessage(*b.output(o).u.queue, m.frame, m.data, m.flags);
            next[o].clear();
        }
    }
};

// What reached its inputs in the last block it ran.
struct Probe {
    static constexpr const char* id = "rp.test.probe";
    struct {
        rp::events_in  events;
        rp::bytes_in   bytes;
        rp::control_in control;
    } inputs;

    std::vector<Message> events, bytes;
    double               controlStart = 0.0;
    std::vector<double>  controlChanges;  // their values, in order
    double               controlAtZero = 0.0;  // the start, then any change at frame 0

    template <class In>
    static void record(const In& in, std::vector<Message>& out) {
        out.clear();
        for (const rp_message& m : in.messages()) {
            const auto data = in.data(m);
            out.push_back({m.frame, std::vector<std::uint8_t>(data.begin(), data.end()), m.flags});
        }
    }
    void process(const rp::Block&) noexcept {
        record(inputs.events, events);
        record(inputs.bytes, bytes);
        controlStart = controlAtZero = inputs.control.start();
        controlChanges.clear();
        for (const rp_control_change& c : inputs.control.changes()) {
            controlChanges.push_back(c.value);
            if (c.frame == 0) controlAtZero = c.value;
        }
    }
};

inline const rp_package* testPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Feed>(), rp::node_type<Probe>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.engine-graph"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::host::test
