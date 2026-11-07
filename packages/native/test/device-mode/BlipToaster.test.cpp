// The bliptoaster script pack (packages/bliptoaster, docs/modular-graph-phase-e4-design.md sections 6 and 7): its one
// node, rp.bliptoaster.midi-policy, on both builds of the MicroQuickJS host, against rp.core.midi-filter on both of
// its builds.
//
// - The twin: the policy drops exactly what the filter drops with BlipToaster's list (polyAftertouch,
//   channelAftertouch, systemCommon, realtime) and passes every other message as it came, its frame, flags and bytes,
//   in order, message for message: every first byte from 0x00 to 0xFF in the lengths a parser makes of it, a SysEx and
//   a lone F7, a message led by a data byte and an empty one, frames tied and flags set, then seeded blocks of any of
//   those, over queues of the box's capacity (rp.device.midi-in's, which feeds the policy on the box).
// - Its output is sized as its input's queue (capacity.match), and it has no settings: its descriptor is its golden
//   (golden/rp.bliptoaster.midi-policy.descriptor.json; RP_GRAPH_GOLDEN_UPDATE=1 records it), as rp.core's types'
//   are theirs.
// - Its pack's heap, at this width, holds the policy behind the box's input: a full queue every block, notes and SysEx,
//   with the live heap after it said, against the pack's declaration.
// - On the desktop, behind a source of the planner's default 256 places, re-plans that hold two instances of it for a
//   while keep it running: the desktop's context is sized for the desktop's queues (script/MqjsHost.hpp's
//   desktopContextBytes).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "DeviceModeEntries.hpp"
#include "graph/GraphHost.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PackageMemory.hpp"
#include "graph/Queue.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/rp_device_midi_in.h"
#include "graph/rp_script.h"
#include "script/MqjsHost.hpp"

extern "C" const rp_package* rp_core_package(void);
extern "C" const rp_package* rp_devtest_plan_package(void);
extern "C" const rp_package* rp_bliptoaster_script_package(void);
extern "C" const rp_package* rp_bliptoaster_script_package_device_mode(void);
extern "C" const rp_mqjs_pack_source rp_script_bliptoaster;

namespace {

using namespace rp::graph;

constexpr const char* kDrop = R"({"drop":["polyAftertouch","channelAftertouch","systemCommon","realtime"]})";
constexpr std::uint32_t kCapacity = RP_DEVICE_MIDI_IN_MESSAGES;
constexpr std::uint32_t kBytes = RP_DEVICE_MIDI_IN_BYTES;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const rp_node_type* typeOf(const rp_package* p, std::string_view id) {
    for (std::uint32_t i = 0; i < p->node_type_count; ++i)
        if (id == p->node_types[i]->id) return p->node_types[i];
    return nullptr;
}

void ignoreLog(void*, int, const char*) {}

// What a script node is made under: its pack's memory (rp.host.packages/1), nothing else. Declared before the nodes.
struct Host {
    PackageMemory packages;
    rp_host       host{RP_ABI_VERSION, this, nullptr, nullptr, &ignoreLog, &Host::interfaceOf};
    static const void* interfaceOf(const rp_host* h, const char* id) {
        auto* self = static_cast<Host*>(h->ctx);
        return std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0 ? self->packages.service() : nullptr;
    }
};

struct Message {
    std::uint32_t             frame = 0;
    std::uint32_t             flags = 0;
    std::vector<std::uint8_t> bytes;
    bool operator==(const Message&) const = default;
};

// One events-in, events-out node made by hand from msgpack settings, its queues at the box's capacity (or another),
// run a block at a time as an executor runs it.
struct Node {
    const rp_node_type*           type = nullptr;
    std::vector<std::max_align_t> memory;
    rp_node*                      node = nullptr;
    std::vector<rp_message>       inItems, outItems;
    std::vector<std::uint8_t>     inBytes = std::vector<std::uint8_t>(kBytes), outBytes = inBytes;
    rp_queue                      in{}, out{};
    rp_port_view                  views[2]{};
    rp_transport                  transport{48000.0, 120.0, 0.0, 0};
    rp_process                    step{};

    Node(const rp_node_type* t, const rp_host* host, const char* json, std::uint32_t capacity = kCapacity)
        : type(t), inItems(capacity), outItems(capacity) {
        in = rp_queue{inItems.data(), 0, capacity, inBytes.data(), 0, kBytes, 0};
        out = rp_queue{outItems.data(), 0, capacity, outBytes.data(), 0, kBytes, 0};
        REQUIRE(type);
        const auto settings = settingsToMsgpack(json);
        REQUIRE(settings);
        const rp_settings s{reinterpret_cast<const char*>(settings->data()), settings->size(), RP_SETTINGS_MSGPACK};
        rp_memory_req req{};
        rp_error      err{};
        REQUIRE(type->query_memory(type, &s, &req, &err));
        memory.resize(req.size / sizeof(std::max_align_t) + 1);
        node = type->create(type, host, memory.data(), &s, &err);
        INFO(err.message);
        REQUIRE(node);
        REQUIRE(node->vt->activate(node, 48000.0, 256, &err));
        views[0].kind = RP_PORT_EVENTS;
        views[0].u.queue = &in;
        views[1].kind = RP_PORT_EVENTS;
        views[1].u.queue = &out;
        const std::uint8_t connected = 1;
        node->vt->ports_changed(node, &connected, 1);
    }
    ~Node() { type->destroy(node); }
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // One block over `messages`, its output.
    std::vector<Message> run(const std::vector<Message>& messages) {
        clearQueue(in);
        clearQueue(out);
        for (const Message& m : messages) REQUIRE(pushMessage(in, m.frame, m.bytes, m.flags));
        step = rp_process{256, &transport, &views[0], 1, &views[1], 1, nullptr, 0};  // the same step every block
        REQUIRE(node->vt->process(node, &step) == RP_OK);
        std::vector<Message> got;
        for (std::uint32_t i = 0; i < out.count; ++i) {
            const auto data = messageData(out, out.items[i]);
            got.push_back({out.items[i].frame, out.items[i].flags, {data.begin(), data.end()}});
        }
        return got;
    }
};

// What a parser makes of a first byte: a channel message is three bytes (two for a program change and channel
// pressure), a SysEx five (F0 .. F7), F1 and F3 two, F2 three, a lone F7 and the rest of system common and real time
// one; a data byte leads a three-byte message of no kind.
std::vector<std::uint8_t> messageFor(std::uint8_t first, std::uint8_t a, std::uint8_t b) {
    if (first < 0x80) return {first, a, b};
    if (first < 0xF0) {
        if ((first & 0xF0) == 0xC0 || (first & 0xF0) == 0xD0) return {first, a};
        return {first, a, b};
    }
    if (first == 0xF0) return {0xF0, 0x7D, a, b, 0xF7};
    if (first == 0xF1 || first == 0xF3) return {first, a};
    if (first == 0xF2) return {first, a, b};
    return {first};
}

// The stream: every first byte once, and an empty message, in blocks of the queue's capacity, frames rising with a tie
// every fourth and the flags moving; then `seeded` blocks of first bytes drawn at random.
std::vector<std::vector<Message>> stream(std::uint32_t seeded) {
    std::vector<Message> all;
    for (std::uint32_t i = 0; i < 256; ++i)
        all.push_back({0, i % 3 == 0 ? 0u : i, messageFor(static_cast<std::uint8_t>(i), i & 0x7f, (i * 7) & 0x7f)});
    all.insert(all.begin() + 100, Message{0, 5, {}});
    std::vector<std::vector<Message>> blocks;
    for (std::size_t at = 0; at < all.size(); at += kCapacity) {
        std::vector<Message> block(all.begin() + at, all.begin() + std::min<std::size_t>(at + kCapacity, all.size()));
        for (std::size_t k = 0; k < block.size(); ++k) block[k].frame = static_cast<std::uint32_t>(k - k / 4);
        blocks.push_back(std::move(block));
    }
    std::uint32_t state = 0x2545F491u;
    auto next = [&] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    for (std::uint32_t b = 0; b < seeded; ++b) {
        std::vector<Message> block;
        std::uint32_t        frame = 0;
        for (std::uint32_t k = next() % (kCapacity + 1); k > 0 && frame < 256; --k, frame += next() % 3) {
            const auto first = static_cast<std::uint8_t>(next());
            block.push_back({frame, next() % 4, messageFor(first, next() & 0x7f, next() & 0x7f)});
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

}  // namespace

TEST_CASE("rp.bliptoaster.midi-policy drops what rp.core.midi-filter drops with BlipToaster's list, message for "
          "message",
          "[device-mode][script-pack][bliptoaster]") {
    struct Pair {
        const char*       name;
        const rp_package* policy;
        const rp_package* filter;
    };
    const Pair pairs[] = {{"desktop", rp_bliptoaster_script_package(), rp_core_package()},
                          {"device", rp_bliptoaster_script_package_device_mode(), rp_core_package_device_mode()}};
    const auto blocks = stream(400);
    std::vector<std::vector<Message>> heard[2];
    for (std::size_t p = 0; p < 2; ++p) {
        INFO(pairs[p].name);
        REQUIRE(pairs[p].policy);
        Host host;
        Node policy(typeOf(pairs[p].policy, "rp.bliptoaster.midi-policy"), &host.host, "{}");
        Node filter(typeOf(pairs[p].filter, "rp.core.midi-filter"), &host.host, kDrop);
        std::size_t passed = 0, dropped = 0;
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            INFO("block " << b);
            const auto mine = policy.run(blocks[b]);
            const auto theirs = filter.run(blocks[b]);
            REQUIRE(mine == theirs);
            heard[p].push_back(mine);
            passed += mine.size();
            dropped += blocks[b].size() - mine.size();
        }
        // The first blocks hold every first byte once: what goes is 0xA0 to 0xAF, 0xD0 to 0xDF, 0xF1 to 0xF6 and 0xF8
        // to 0xFF, 46 of the 257 (the empty message among those that stay).
        std::size_t firstDropped = 0;
        for (std::size_t b = 0; b * kCapacity < 257; ++b) {
            const auto mine = policy.run(blocks[b]);
            firstDropped += blocks[b].size() - mine.size();
        }
        CHECK(firstDropped == 16 + 16 + 6 + 8);
        CHECK(passed > dropped);
        CHECK(dropped > 0);
    }
    // Both builds of the policy heard the same.
    CHECK(heard[0] == heard[1]);
}

TEST_CASE("rp.bliptoaster.midi-policy's descriptor is its golden", "[device-mode][script-pack][bliptoaster]") {
    const rp_package* pack = rp_bliptoaster_script_package();
    REQUIRE(pack);
    CHECK(std::string(pack->manifest_json) == R"({"id":"rp.bliptoaster.script","version":"1.0.0"})");
    REQUIRE(pack->node_type_count == 1);
    const rp_node_type* type = pack->node_types[0];
    CHECK(std::string(type->id) == "rp.bliptoaster.midi-policy");
    const std::string json = type->descriptor(type);
    const std::string path =
        std::string(RP_DEVICE_MODE_GOLDEN_DIR) + "/rp.bliptoaster.midi-policy.descriptor.json";
    if (std::getenv("RP_GRAPH_GOLDEN_UPDATE")) std::ofstream(path) << json << "\n";
    CHECK(json + "\n" == readFile(path));
    // The same port layout as the filter's, which it stands in for on the box: events in, an output that matches it.
    CHECK(json.find(R"("ports":[{"id":"in","dir":"in","kind":"events"},{"id":"out","dir":"out","kind":"events",)"
                    R"("capacity":{"match":"in"}}],"params":[])") != std::string::npos);
}

TEST_CASE("the bliptoaster pack's heap holds its policy behind the box's input, full every block",
          "[device-mode][script-pack][bliptoaster]") {
    for (const rp_package* pack : {rp_bliptoaster_script_package(), rp_bliptoaster_script_package_device_mode()}) {
        INFO((pack == rp_bliptoaster_script_package() ? "desktop" : "device"));
        Host host;
        Node policy(typeOf(pack, "rp.bliptoaster.midi-policy"), &host.host, "{}");
        rp_script_heap_use use{};
        for (std::uint32_t b = 0; b < 64; ++b) {
            std::vector<Message> block;
            if (b % 2 == 0) {
                for (std::uint32_t k = 0; k < kCapacity; ++k)
                    block.push_back({k * 256 / kCapacity, 0, {static_cast<std::uint8_t>(k % 2 ? 0x80 : 0x90),
                                                             static_cast<std::uint8_t>(48 + (b + k) % 24), 100}});
            } else {
                for (std::uint32_t k = 0; k < kBytes / 64; ++k) {
                    std::vector<std::uint8_t> sysex(64, static_cast<std::uint8_t>(k & 0x7f));
                    sysex.front() = 0xF0;
                    sysex.back() = 0xF7;
                    block.push_back({k * 4, 0, sysex});
                }
            }
            CHECK(policy.run(block).size() == block.size());
        }
        const auto* heap =
            static_cast<const rp_script_heap*>(policy.node->vt->get_interface(policy.node, RP_SCRIPT_HEAP_INTERFACE));
        REQUIRE(heap);
        REQUIRE(heap->use(policy.node, true, &use));
        const std::uint32_t declared = rp_script_bliptoaster.heap_bytes;
        INFO("bliptoaster's heap at 64 bits after 64 full blocks: " << use.fixed + use.heap << " live of "
                                                                    << use.bytes << " (declared " << declared
                                                                    << ", the box's)");
        // The device's build gives the context the toolchain check's 3/2 of the box's figure (kDeviceHalves), and
        // what the policy holds after full blocks fits it; the desktop's gives it far more (desktopContextBytes()).
        const std::size_t checked = std::size_t{declared} * rp::mqjs::kDeviceHalves / 2;
        CHECK(use.bytes >= checked);
        CHECK(use.fixed + use.heap < checked);
    }
}

TEST_CASE("a block's SysEx payloads die with it: the policy's heap holds no block's past its end",
          "[device-mode][script-pack][bliptoaster]") {
    // A payload longer than three bytes is a Uint8Array made for its block. A place kept it until a later block
    // refilled the place, so blocks that each put a SysEx at a place the blocks after them fill no further (k notes
    // and then a SysEx of the queue's other bytes, k from 63 down to 1) kept one at every place: the device build's
    // context, 3/2 of the box's 24576 bytes, ran out within a few blocks, at any heap the box could give it. Each
    // block's payloads are let go of as it ends, so what the context holds after every block, collected, is what it held after a block of
    // notes, on both builds; and every message comes out.
    for (const rp_package* pack : {rp_bliptoaster_script_package(), rp_bliptoaster_script_package_device_mode()}) {
        INFO((pack == rp_bliptoaster_script_package() ? "desktop" : "device"));
        Host host;
        Node policy(typeOf(pack, "rp.bliptoaster.midi-policy"), &host.host, "{}");
        const auto* heap =
            static_cast<const rp_script_heap*>(policy.node->vt->get_interface(policy.node, RP_SCRIPT_HEAP_INTERFACE));
        REQUIRE(heap);
        auto inUse = [&] {
            rp_script_heap_use use{};
            REQUIRE(heap->use(policy.node, true, &use));
            return use.fixed + use.heap;
        };
        auto sysex = [](std::uint32_t frame, std::uint32_t size) {
            std::vector<std::uint8_t> bytes(size, static_cast<std::uint8_t>(frame & 0x7f));
            bytes.front() = 0xF0;
            bytes.back() = 0xF7;
            return Message{frame, 0, bytes};
        };
        std::vector<Message> notes;
        for (std::uint32_t k = 0; k < kCapacity; ++k)
            notes.push_back({k, 0, {0x90, static_cast<std::uint8_t>(36 + k % 48), 100}});
        CHECK(policy.run(notes).size() == notes.size());
        const std::size_t settled = inUse();
        for (std::uint32_t k = kCapacity - 1; k >= 1; --k) {
            INFO(k << " notes and a SysEx");
            std::vector<Message> block(notes.begin(), notes.begin() + k);
            block.push_back(sysex(k, kBytes - 3 * k));
            CHECK(policy.run(block).size() == block.size());
            CHECK(inUse() == settled);
        }
        std::vector<Message> full;
        for (std::uint32_t k = 0; k < kCapacity; ++k) full.push_back(sysex(k, kBytes / kCapacity));
        CHECK(policy.run(full).size() == full.size());
        CHECK(inUse() == settled);
        CHECK(policy.run({sysex(0, kBytes)}).size() == 1);
        CHECK(inUse() == settled);
    }
}

TEST_CASE("on the desktop, the policy behind a 256-place source runs through re-plans that hold two of it",
          "[device-mode][script-pack][bliptoaster]") {
    // Re-plans on a GraphHost, the policy behind rp.devtest.source's events (the planner's default 256 places): a new
    // id each time, so a new instance, made at its first block. This thread stands in for the audio thread (the host
    // owned by it), so a replaced plan is handed back at the block that adopts the next and kept until reclaim(),
    // after the round's blocks: the old instance and its pools stay in the pack's heap beside the new one's for those
    // blocks, as on a desktop host whose control thread reclaims later.
    NodeRegistry registry;
    for (const rp_package* p : {rp_devtest_plan_package(), rp_bliptoaster_script_package()}) {
        std::string why;
        INFO(why);
        REQUIRE(registry.addPackage(p, &why));
    }
    ResourceTable resources;
    GraphHost     graph(registry, resources, HostConfig{48000.0, 256, Target::Desktop});
    const rp_transport transport{48000.0, 120.0, 0.0, 0};
    graph.setAudioThreadOwns(true);
    std::size_t reclaimed = 0;
    for (int round = 0; round < 8; ++round) {
        const std::string id = "policy" + std::to_string(round);
        REQUIRE(graph.setGraph(R"({"nodes":[{"id":"src","type":"rp.devtest.source"},{"id":")" + id +
                               R"(","type":"rp.bliptoaster.midi-policy"}],"wires":[{"from":"src.events","to":")" + id +
                               R"(.in"}]})")
                    .ok);
        for (int b = 0; b < 4; ++b) graph.processBlock(256, transport);
        reclaimed += graph.reclaim();
    }
    graph.setAudioThreadOwns(false);
    CHECK(reclaimed >= 7);  // each replaced plan was held to the round's end
    CHECK(graph.faultCount() == 0);

    // The same two instances by hand, in one host's pack memory, behind full 256-place queues of notes and of SysEx:
    // more than a context sized for the box's 64 places would hold, at the desktop's factors.
    Host host;
    Node first(typeOf(rp_bliptoaster_script_package(), "rp.bliptoaster.midi-policy"), &host.host, "{}", 256);
    Node second(typeOf(rp_bliptoaster_script_package(), "rp.bliptoaster.midi-policy"), &host.host, "{}", 256);
    std::size_t most = 0;
    for (std::uint32_t b = 0; b < 16; ++b) {
        std::vector<Message> block;
        if (b % 2 == 0) {
            for (std::uint32_t k = 0; k < 256; ++k)
                block.push_back({k, 0, {0x90, static_cast<std::uint8_t>(48 + (b + k) % 24), 100}});
        } else {
            for (std::uint32_t k = 0; k < kBytes / 16; ++k) {
                std::vector<std::uint8_t> sysex(16, static_cast<std::uint8_t>(k & 0x7f));
                sysex.front() = 0xF0;
                sysex.back() = 0xF7;
                block.push_back({k, 0, sysex});
            }
        }
        CHECK(first.run(block).size() == block.size());
        CHECK(second.run(block).size() == block.size());
        const auto* heap =
            static_cast<const rp_script_heap*>(first.node->vt->get_interface(first.node, RP_SCRIPT_HEAP_INTERFACE));
        rp_script_heap_use use{};
        REQUIRE(heap);
        REQUIRE(heap->use(first.node, true, &use));
        most = std::max(most, use.fixed + use.heap);
    }
    INFO("the pack's heap held at most " << most << " bytes, of "
                                          << rp::mqjs::desktopContextBytes(rp_script_bliptoaster));
    CHECK(most > std::size_t{rp_script_bliptoaster.heap_bytes} * rp::mqjs::kWidthFactor * rp::mqjs::kDesktopHeapFactor);
}
