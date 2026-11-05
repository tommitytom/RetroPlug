// rp.device.midi-in (packages/device-io, docs/modular-graph-phase-e3-design.md section 5), its device build and its
// desktop build alike, each made and run through the C ABI as a host makes it, over a fake rp.device.midi-in/1
// (FakeMidiIn.hpp).
//
// - It takes what the host staged for the block, in the host's order, at the host's frames: a frame at or past the
//   block's end lands on its last frame, one earlier than the last taken is clamped up to it, an empty message is not
//   pushed, and a block with nothing staged is empty. The host is told the block's length.
// - A full queue (256 messages, 4 KB) drops the rest and counts them as the queue's, and a message longer than the
//   queue's 4 KB is dropped and counted before its length reaches a push, whatever length the host answers.
// - A host that offers no rp.device.midi-in/1: it activates, every block is empty, and nothing faults.
// - The device build's create, activate, process and destroy allocate nothing; one a plan (`unique`).
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "DeviceModeEntries.hpp"
#include "FakeMidiIn.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/GraphHost.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/Queue.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/rp_device_midi_in.h"

namespace {

using rp::graph::devtest::FakeMidiIn;

void ignoreLog(void*, int, const char*) {}

// A host offering the fake's interface, or (with none) nothing at all.
struct MidiHost {
    FakeMidiIn* midi = nullptr;
    rp_host     host{RP_ABI_VERSION, this, nullptr, nullptr, &ignoreLog, &interface};
    static const void* interface(const rp_host* h, const char* id) {
        const auto* self = static_cast<const MidiHost*>(h->ctx);
        return self->midi && std::strcmp(id, RP_DEVICE_MIDI_IN_INTERFACE) == 0 ? &self->midi->iface : nullptr;
    }
};

const rp_node_type* midiInOf(const rp_package* p) {
    for (std::uint32_t i = 0; i < p->node_type_count; ++i)
        if (std::string_view(p->node_types[i]->id) == "rp.device.midi-in") return p->node_types[i];
    return nullptr;
}

// One instance, made from msgpack's empty map (the node has no settings), activated, and run a block at a time into a
// queue of the node's own capacity, as a plan would give it.
struct Made {
    const rp_node_type*           type;
    std::vector<std::max_align_t> memory;
    rp_node*                      node = nullptr;
    bool                          active = false;
    std::vector<rp_message>       items = std::vector<rp_message>(RP_DEVICE_MIDI_IN_MESSAGES);
    std::vector<std::uint8_t>     bytes = std::vector<std::uint8_t>(RP_DEVICE_MIDI_IN_BYTES);
    rp_queue queue{items.data(), 0, RP_DEVICE_MIDI_IN_MESSAGES, bytes.data(), 0, RP_DEVICE_MIDI_IN_BYTES, 0};
    rp_transport transport{48000.0, 120.0, 0.0, 0};

    Made(const rp_node_type* t, const rp_host* host) : type(t) {
        static const std::uint8_t kEmptyMap[] = {0x80};
        const rp_settings s{reinterpret_cast<const char*>(kEmptyMap), sizeof kEmptyMap, RP_SETTINGS_MSGPACK};
        rp_memory_req req{};
        rp_error      err{};
        REQUIRE(t->query_memory(t, &s, &req, &err));
        REQUIRE(req.align <= alignof(std::max_align_t));
        memory.resize(req.size / sizeof(std::max_align_t) + 1);
        node = t->create(t, host, memory.data(), &s, &err);
        INFO(err.message);
        REQUIRE(node);
        active = node->vt->activate(node, 48000.0, 1024, &err);
    }
    ~Made() {
        if (active) node->vt->deactivate(node);
        if (node) type->destroy(node);
    }
    Made(const Made&) = delete;
    Made& operator=(const Made&) = delete;

    rp_status block(std::uint32_t frames) {
        rp::graph::clearQueue(queue);
        queue.dropped = 0;
        rp_port_view out{};
        out.kind = RP_PORT_EVENTS;
        out.u.queue = &queue;
        const rp_process p{frames, &transport, nullptr, 0, &out, 1, nullptr, 0};
        return node->vt->process(node, &p);
    }
    // One line a message: frame and bytes.
    std::string taken() const {
        std::ostringstream o;
        for (std::uint32_t i = 0; i < queue.count; ++i) {
            const rp_message& m = queue.items[i];
            o << m.frame << ":";
            for (std::uint32_t k = 0; k < m.size; ++k) o << " " << std::hex << int(queue.bytes[m.offset + k]) << std::dec;
            o << "\n";
        }
        return o.str();
    }
};

// Both builds of the type: the device's, then the desktop's.
std::vector<const rp_node_type*> builds() {
    return {midiInOf(rp_device_io_package_device_mode()), midiInOf(rp_device_io_package())};
}

}  // namespace

TEST_CASE("rp.device.midi-in takes what its host staged, at frames within the block, built for the device as for the "
          "desktop",
          "[device-mode][device-io]") {
    for (const rp_node_type* type : builds()) {
        REQUIRE(type);
        INFO((type == builds()[0] ? "device" : "desktop"));
        FakeMidiIn midi;
        MidiHost   host{&midi};
        Made       made(type, &host.host);
        REQUIRE(made.active);
        midi.stage(0, {0x90, 0x3c, 0x64});
        midi.stage(5, {0x80, 0x3c, 0x00});
        midi.stage(5, {0xB0, 0x07, 0x7f});
        midi.stage(63, {0xF8});
        midi.stage(64, {0xFA});              // at the block's end: its last frame
        midi.stage(70, {0xFC});              // past it: the same
        midi.stage(10, {0x90, 0x3e, 0x64});  // earlier than the last taken: clamped up to it
        midi.stage(12, {});                  // carries nothing: not pushed
        midi.stage(63, {0xF0, 0x7D, 0x50, 0x01, 0xF7});
        CHECK(made.block(64) == RP_OK);
        CHECK(made.taken() == "0: 90 3c 64\n5: 80 3c 0\n5: b0 7 7f\n63: f8\n63: fa\n63: fc\n63: 90 3e 64\n"
                              "63: f0 7d 50 1 f7\n");
        CHECK(made.queue.dropped == 0);
        CHECK(midi.lastFrames == 64);
        CHECK(midi.calls == 10);  // nine messages, and the answer that none is left
        // Nothing staged: an empty block.
        CHECK(made.block(32) == RP_OK);
        CHECK(made.queue.count == 0);
        CHECK(midi.lastFrames == 32);
        CHECK(midi.calls == 11);
        // A one-frame block: everything at frame 0.
        midi.stage(0, {0xF8});
        midi.stage(9, {0xF8});
        CHECK(made.block(1) == RP_OK);
        CHECK(made.taken() == "0: f8\n0: f8\n");
    }
}

TEST_CASE("rp.device.midi-in drops what passes its 256 messages or 4 KB, and counts it", "[device-mode][device-io]") {
    for (const rp_node_type* type : builds()) {
        INFO((type == builds()[0] ? "device" : "desktop"));
        FakeMidiIn midi;
        MidiHost   host{&midi};
        Made       made(type, &host.host);
        for (std::uint32_t i = 0; i < 300; ++i) midi.stage(i * 256 / 300, {0x90, static_cast<std::uint8_t>(i & 0x7f), 1});
        CHECK(made.block(256) == RP_OK);
        CHECK(made.queue.count == RP_DEVICE_MIDI_IN_MESSAGES);
        CHECK(made.queue.dropped == 300 - RP_DEVICE_MIDI_IN_MESSAGES);
        CHECK(made.queue.items[255].frame == 255 * 256 / 300);  // the first 256, as staged
        // Two SysEx messages of 3000 bytes: the first fits the 4 KB, the second does not.
        std::vector<std::uint8_t> sysex(3000, 0x11);
        sysex.front() = 0xF0;
        sysex.back() = 0xF7;
        midi.stage(0, sysex);
        midi.stage(1, sysex);
        midi.stage(2, {0xF8});  // and a clock after it still fits
        CHECK(made.block(256) == RP_OK);
        CHECK(made.queue.count == 2);
        CHECK(made.queue.items[0].size == 3000);
        CHECK(made.queue.items[1].size == 1);
        CHECK(made.queue.dropped == 1);
        // A length past the queue's bytes, among small messages: 4097, and 0xFFFFFFFF after the one byte of F8 (a sum
        // that wraps to 0 in a 32-bit size_t), each dropped and counted; nothing is read from the bytes behind them.
        midi.stage(0, {0xF8});
        midi.stageClaiming(1, {0xF0, 0xF7}, RP_DEVICE_MIDI_IN_BYTES + 1);
        midi.stageClaiming(2, {0xF0, 0xF7}, 0xFFFFFFFFu);
        midi.stage(3, {0xFA});
        CHECK(made.block(256) == RP_OK);
        CHECK(made.taken() == "0: f8\n3: fa\n");
        CHECK(made.queue.dropped == 2);
        CHECK(made.queue.bytes_used == 2);
    }
}

TEST_CASE("rp.device.midi-in in a host that offers no rp.device.midi-in/1 activates, and gives empty blocks",
          "[device-mode][device-io]") {
    for (const rp_node_type* type : builds()) {
        INFO((type == builds()[0] ? "device" : "desktop"));
        MidiHost host;  // offers nothing
        Made     made(type, &host.host);
        CHECK(made.active);
        for (int i = 0; i < 3; ++i) {
            CHECK(made.block(64) == RP_OK);
            CHECK(made.queue.count == 0);
            CHECK(made.queue.dropped == 0);
        }
    }
}

TEST_CASE("rp.device.midi-in's device build allocates nothing, and a plan holds one", "[device-mode][device-io]") {
    FakeMidiIn midi;
    MidiHost   host{&midi};
    midi.staged.reserve(16);
    for (std::uint32_t i = 0; i < 12; ++i) midi.stage(i * 5, {0x90, static_cast<std::uint8_t>(i), 100});
    const rp_node_type* type = builds()[0];
    // The instance's memory and its queue are the host's, made before the window opens; its query_memory, create,
    // activate, block, deactivate and destroy are inside it. The window holds no Catch2 macro (which allocates).
    std::vector<std::max_align_t> memory(64);
    std::vector<rp_message>       items(RP_DEVICE_MIDI_IN_MESSAGES);
    std::vector<std::uint8_t>     bytes(RP_DEVICE_MIDI_IN_BYTES);
    static const std::uint8_t     kEmptyMap[] = {0x80};
    std::uint32_t                 count = 0;
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    {
        const rp_settings s{reinterpret_cast<const char*>(kEmptyMap), sizeof kEmptyMap, RP_SETTINGS_MSGPACK};
        rp_memory_req     req{};
        rp_error          err{};
        const bool        sized = type->query_memory(type, &s, &req, &err) && req.size <= memory.size() * sizeof memory[0];
        rp_node*          node = sized ? type->create(type, &host.host, memory.data(), &s, &err) : nullptr;
        if (node && node->vt->activate(node, 48000.0, 1024, &err)) {
            rp_queue     q{items.data(), 0, RP_DEVICE_MIDI_IN_MESSAGES, bytes.data(), 0, RP_DEVICE_MIDI_IN_BYTES, 0};
            rp_port_view out{};
            out.kind = RP_PORT_EVENTS;
            out.u.queue = &q;
            const rp_transport t{48000.0, 120.0, 0.0, 0};
            const rp_process   p{64, &t, nullptr, 0, &out, 1, nullptr, 0};
            node->vt->process(node, &p);
            count = q.count;
            node->vt->deactivate(node);
        }
        if (node) type->destroy(node);
    }
    const auto counted = rp::test::endCountingAllocations();
    CHECK(count == 12);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);

    // `unique`, as the desktop's descriptor says and its planner holds.
    rp::graph::NodeRegistry registry;
    REQUIRE(registry.addPackage(rp_device_io_package()));
    rp::graph::ResourceTable resources;
    rp::graph::GraphHost     graph(registry, resources, rp::graph::HostConfig{48000.0, 256, rp::graph::Target::Pico});
    CHECK(graph.setGraph(R"({"nodes":[{"id":"a","type":"rp.device.midi-in"}],"wires":[]})").ok);
    const auto two = graph.setGraph(R"({"nodes":[{"id":"a","type":"rp.device.midi-in"},)"
                                    R"({"id":"b","type":"rp.device.midi-in"}],"wires":[]})");
    REQUIRE_FALSE(two.ok);
    CHECK(two.errors[0].code == "unique");
    CHECK(two.errors[0].nodeId == "b");
}
