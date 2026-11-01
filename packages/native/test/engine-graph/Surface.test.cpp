// rp.launchpad.surface-in / -out, a Novation Launchpad as a control surface on nodes of its own
// (docs/modular-graph-phase-c5-design.md section 5): the device's MIDI drained from the host's surface link into the
// input node's `in`, a `connected` control output that follows the device, and what a session writes into the output
// node's `out` handed back to the link in the block it queued it. The nodes bind the link by name through
// rp.host.devices/1, as rp.n8.nes binds the cart's, so a host with no surface plans and runs the same document. Two
// nodes rather than one because a session is fed by the device and feeds it: one node would be a cycle the planner
// refuses, and breaking it on a feedback wire would deliver the LED stream a block late.
//
// The link here is stage 1's fake: what it was scripted to send, what a node handed it, and the nodes holding it.
// ProtocolNodes.test.cpp holds the session behind these nodes to its recorded digests; these cases hold the nodes
// themselves, their acquire and release, the missing device, the uniqueness rule the types declare, and a warmed
// block's allocation count.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[launchpad]"`.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "FakeSurfaceLink.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "graph/node.hpp"
#include "host/devices/DeviceLinks.hpp"
#include "host/graph/LaunchpadNodes.hpp"

namespace {

using rp::host::kMaxDeviceMessage;
using rp::host::kSurfaceInBytes;
using rp::host::test::deviceMessage;
using rp::host::test::DocExtras;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::FakeSurfaceLink;
using rp::host::test::Feed;
using rp::host::test::Message;
using rp::host::test::NodeRig;
using rp::host::test::Probe;
using Bytes = std::vector<std::uint8_t>;

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 64;

void block(NodeRig& rig) { rig.host->processBlock(kFrames, rp_transport{kRate, 120.0, 0.0, 0}); }

// The plan the rig made, or the reasons it refused it.
void plan(NodeRig& rig, const std::string& document) {
    const auto r = rig.set(document);
    for (const auto& e : r.errors) UNSCOPED_INFO(e.code << " at " << e.nodeId << "." << e.port << ": " << e.message);
    REQUIRE(r.ok);
}

// Both surface nodes beside whatever a case adds, wired as the case's wires say: a case uses the input node's ports or
// the output node's, and the other runs with nothing wired to it.
std::string surfaceDocument(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::vector<std::string> all{docNode("surface-in", "rp.launchpad.surface-in"),
                                 docNode("surface-out", "rp.launchpad.surface-out")};
    all.insert(all.end(), nodes.begin(), nodes.end());
    return rp::host::test::docs_detail::document(all, wires);
}

std::string plainDocument(const std::vector<std::string>& nodes) {
    return rp::host::test::docs_detail::document(nodes, {});
}

// A node that writes the same two messages into its one output every block, from its own storage: a counted block
// must exercise the surface's out without a test node's own vectors freeing their payloads (Devices.test.cpp's probe
// pattern, cut to what a surface's out needs).
struct ScriptedOut {
    static constexpr const char* id = "rp.test.surface-out";
    struct { rp::events_out::sized<rp::capacity{.messages = 4, .bytes = 2048}> out; } outputs;

    void process(const rp::Block&) noexcept {
        static constexpr std::uint8_t kShort[] = {0xB0, 0x10, 0x7F};
        static const std::array<std::uint8_t, kMaxDeviceMessage> kWide = [] {
            std::array<std::uint8_t, kMaxDeviceMessage> bytes{};
            bytes.fill(0x22);
            return bytes;
        }();
        outputs.out.push(0, kShort);
        outputs.out.push(4, kWide);
    }
};

const rp_package* surfaceTestPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<ScriptedOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.surface"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

// The node type a package registers, so a case can read its descriptor without planning it.
const rp_node_type* type(const rp_package* package, const char* id) {
    for (std::uint32_t i = 0; i < package->node_type_count; ++i)
        if (std::string(package->node_types[i]->id) == id) return package->node_types[i];
    return nullptr;
}

}  // namespace

TEST_CASE("a surface's in carries what the device sent, in order, every message at frame 0", "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    plan(rig, surfaceDocument({docNode("probe", "rp.test.probe")}, {docWire("surface-in.in", "probe.events")}));

    // The node's first block discards what the link held when it bound it (the next case); this one scripts traffic
    // with that block out of the way.
    block(rig);

    // More than one drain batch, and one message at the link's widest, so the node's drain loop and a full-size slot
    // are both exercised in one block. The device's traffic carries no frame of its own: every message lands at frame
    // 0, a control surface's timing being no musical one.
    std::vector<std::uint8_t> wide(kMaxDeviceMessage, 0x5A);
    surface.input.push_back(deviceMessage(wide));
    for (int i = 0; i < 40; ++i)
        surface.input.push_back(deviceMessage({0x90, static_cast<std::uint8_t>(11 + i), 100}));
    block(rig);

    const std::vector<Message>& events = rig.node<Probe>("probe").events;
    REQUIRE(events.size() == surface.input.size());
    CHECK(events[0].data == wide);
    for (std::size_t i = 0; i < events.size(); ++i) {
        INFO("message " << i);
        CHECK(events[i].frame == 0);
        if (i > 0) CHECK(events[i].data == Bytes{0x90, static_cast<std::uint8_t>(10 + i), 100});
    }
    CHECK(rig.host->droppedMessages() == 0);
}

TEST_CASE("a surface's out reaches the link in queue order, and a detached link takes nothing", "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    plan(rig, surfaceDocument({docNode("feed", "rp.test.feed")}, {docWire("feed.midi", "surface-out.out")}));

    Feed& feed = rig.node<Feed>("feed");
    feed.next[Feed::Midi] = {{0, {0xB0, 0x10, 0x7F}}, {9, {0xF0, 0x7D, 0x01, 0xF7}}, {63, {0x90, 0x51, 0x00}}};
    block(rig);
    REQUIRE(surface.outputs.size() == 3);
    CHECK(Bytes(surface.outputs[0].data, surface.outputs[0].data + surface.outputs[0].size) == Bytes{0xB0, 0x10, 0x7F});
    CHECK(Bytes(surface.outputs[1].data, surface.outputs[1].data + surface.outputs[1].size) ==
          Bytes{0xF0, 0x7D, 0x01, 0xF7});
    CHECK(Bytes(surface.outputs[2].data, surface.outputs[2].data + surface.outputs[2].size) == Bytes{0x90, 0x51, 0x00});

    // Unplugged: the same messages go nowhere, and the link counts them itself - the node has nothing to send them to
    // but the link, so the count is the link's.
    surface.outputs.clear();
    surface.attached = false;
    feed.next[Feed::Midi] = {{0, {0x90, 0x51, 0x7F}}};
    block(rig);
    CHECK(surface.outputs.empty());
    CHECK(surface.dropped() == 1);
    CHECK(rig.host->faultCount() == 0);
}

TEST_CASE("a surface's connected follows the link, from frame 0", "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    plan(rig, surfaceDocument({docNode("probe", "rp.test.probe")}, {docWire("surface-in.connected", "probe.control")}));
    surface.attached = false;
    block(rig);
    const Probe& probe = rig.node<Probe>("probe");
    CHECK(probe.controlStart == 0.0);
    CHECK(probe.controlChanges.empty());

    // Attached: the flag flips at frame 0 of that block, so a session reading it takes the device on the block it
    // appears.
    surface.attached = true;
    block(rig);
    CHECK(probe.controlStart == 0.0);
    CHECK(probe.controlAtZero == 1.0);
    CHECK(probe.controlChanges == std::vector<double>{1.0});
    block(rig);  // held: no second change
    CHECK(probe.controlStart == 1.0);
    CHECK(probe.controlChanges.empty());

    surface.attached = false;
    block(rig);
    CHECK(probe.controlChanges == std::vector<double>{0.0});
}

TEST_CASE("the surface nodes in a host with no device plan and run with connected 0", "[launchpad]") {
    // The registry answers a name its host never installed with a handle that is never connected, so the same document
    // plans in the plugin, the CLI and every harness, and nothing needs a null check.
    NodeRig rig(kRate);  // no devices.install at all
    plan(rig, surfaceDocument({docNode("probe", "rp.test.probe")},
                              {docWire("surface-in.in", "probe.events"),
                               docWire("surface-in.connected", "probe.control")}));
    block(rig);
    CHECK(rig.node<Probe>("probe").events.empty());
    CHECK(rig.node<Probe>("probe").controlAtZero == 0.0);
    CHECK(rig.host->droppedMessages() == 0);
    CHECK(rig.host->faultCount() == 0);
}

TEST_CASE("a surface's in drops what its queue has no room for, and counts it", "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    plan(rig, surfaceDocument({docNode("probe", "rp.test.probe")}, {docWire("surface-in.in", "probe.events")}));

    // 1100 messages of 20 bytes against rp.launchpad.surface-in's 1024 messages and 16384 bytes: the byte bound binds
    // first, at 819 messages (16380 bytes), and the 281 the queue cannot hold are dropped and counted there. The link
    // is drained either way, so nothing accumulates behind a full queue. The first block discards what the link bound
    // with (the next case), so it runs before this traffic is scripted.
    block(rig);
    const Bytes twenty(20, 0x11);
    for (int i = 0; i < 1100; ++i) surface.input.push_back(deviceMessage(twenty));
    block(rig);
    const std::size_t held = kSurfaceInBytes / twenty.size();
    CHECK(held == 819);
    CHECK(rig.node<Probe>("probe").events.size() == held);
    CHECK(rig.host->droppedMessages("surface-in") == 1100 - held);

    // The next block starts afresh: the queue is emptied by the plan, and the link has nothing left to give.
    block(rig);
    CHECK(rig.node<Probe>("probe").events.empty());
    CHECK(rig.host->droppedMessages("surface-in") == 1100 - held);
}

TEST_CASE("a surface's in discards what the link held when it bound it, and a reactivate keeps delivering",
          "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    // Presses sent while no node read the device wait in the link (a project with the controller off drains nothing):
    // a session that starts now would replay them, so the node's first block drains the link and forwards none of it.
    surface.input.push_back(deviceMessage({0x90, 0x33, 100}));
    plan(rig, surfaceDocument({docNode("probe", "rp.test.probe")}, {docWire("surface-in.in", "probe.events")}));
    block(rig);
    CHECK(rig.node<Probe>("probe").events.empty());
    // Drained, not kept: the block after has nothing to deliver either.
    block(rig);
    CHECK(rig.node<Probe>("probe").events.empty());
    // A press after the first block is delivered, as every one after it is.
    surface.input.push_back(deviceMessage({0x90, 0x34, 100}));
    block(rig);
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].data == Bytes{0x90, 0x34, 100});
    // A reactivate that keeps the handle (the new rate and size a sample-rate change re-plans for) is not a first
    // block: the instance already read the link before and after it, so what arrived is delivered.
    surface.input.push_back(deviceMessage({0x90, 0x35, 100}));
    std::string why;
    REQUIRE(rig.host->reactivate(kRate, 8192, &why));
    CHECK(surface.acquires == 2);  // the handle it bound is the one it keeps
    block(rig);
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].data == Bytes{0x90, 0x35, 100});
    CHECK(rig.host->faultCount() == 0);
}

TEST_CASE("a plan holds one surface input node and one surface output node", "[launchpad]") {
    // One device, one link: a second input node would drain the one ring its twin drains (and the device would answer
    // two sessions), and a second output node would split the LED frames, so both types declare themselves unique and
    // the planner refuses the document.
    const rp_package* package = rp::host::launchpadPackage();
    const rp_node_type* surfaceIn = type(package, "rp.launchpad.surface-in");
    const rp_node_type* surfaceOut = type(package, "rp.launchpad.surface-out");
    REQUIRE(surfaceIn != nullptr);
    REQUIRE(surfaceOut != nullptr);
    for (const rp_node_type* t : {surfaceIn, surfaceOut}) {
        const std::string descriptor(t->descriptor(t));
        INFO(t->id);
        CHECK(descriptor.find(R"("unique":true)") != std::string::npos);
        CHECK(descriptor.find(R"("threads":true)") != std::string::npos);
    }
    // A type that declares neither spells neither.
    const rp_node_type* probeType = type(rp::host::test::testPackage(), "rp.test.probe");
    REQUIRE(probeType != nullptr);
    CHECK(std::string(probeType->descriptor(probeType)).find("unique") == std::string::npos);

    NodeRig rig(kRate);
    for (const char* id : {"rp.launchpad.surface-in", "rp.launchpad.surface-out"}) {
        INFO(id);
        const auto two = rig.set(plainDocument({docNode("one", id), docNode("two", id)}));
        REQUIRE_FALSE(two.ok);
        CHECK(two.errors.at(0).code == "unique");
        CHECK(two.errors.at(0).nodeId == "two");
        CHECK(two.errors.at(0).message.find(id) != std::string::npos);
    }

    // rp.n8.nes carries the same rule (the design's own device, placed by stage 5's setting), so a document that
    // drives the cart twice is refused for the same reason, naming the second.
    const auto carts = rig.set(plainDocument({docNode("cart-a", "rp.n8.nes"), docNode("cart-b", "rp.n8.nes")}));
    REQUIRE_FALSE(carts.ok);
    CHECK(carts.errors.at(0).code == "unique");
    CHECK(carts.errors.at(0).nodeId == "cart-b");
    CHECK(carts.errors.at(0).message.find("rp.n8.nes") != std::string::npos);

    // One of each is the plan buildGraph makes when both settings are on.
    CHECK(rig.set(plainDocument({docNode("surface-in", "rp.launchpad.surface-in"),
                                 docNode("surface-out", "rp.launchpad.surface-out"), docNode("cart", "rp.n8.nes")}))
              .ok);
}

TEST_CASE("the surface types declare threads, so a target with none refuses them", "[launchpad]") {
    NodeRig pico(kRate, 8192, rp::graph::Target::Pico);
    for (const char* id : {"rp.launchpad.surface-in", "rp.launchpad.surface-out"}) {
        INFO(id);
        const auto refused = pico.set(plainDocument({docNode("surface", id)}));
        REQUIRE_FALSE(refused.ok);
        CHECK(refused.errors.at(0).code == "capability");
        CHECK(refused.errors.at(0).message.find("threads") != std::string::npos);
    }
}

TEST_CASE("the surface nodes take the link once each, keep it across a reactivate, and give it back once",
          "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    rig.devices.install(nullptr, &surface);
    plan(rig, surfaceDocument({}, {}));
    // Two nodes, two holds on the one link: the registry forwards every acquire a node makes, so an unbalanced one is
    // visible only here.
    CHECK(surface.acquires == 2);
    CHECK(surface.releases == 0);

    // A sample-rate change and a block growth each deactivate and activate every instance again
    // (GraphHost::reactivate). A node that already holds its handle keeps it, so neither takes the link again.
    std::string why;
    REQUIRE(rig.host->reactivate(44100.0, 8192, &why));
    CHECK(surface.acquires == 2);
    CHECK(surface.releases == 0);
    REQUIRE(rig.host->reactivate(44100.0, 16384, &why));
    CHECK(surface.acquires == 2);
    CHECK(surface.releases == 0);

    // The plan's reclaim runs both destructors, each giving its handle back once, and only once.
    REQUIRE(rig.set(plainDocument({})).ok);
    rig.host->reclaim();
    CHECK(surface.acquires == 2);
    CHECK(surface.releases == 2);
}

TEST_CASE("a warmed block of the surface nodes allocates and frees nothing", "[launchpad]") {
    FakeSurfaceLink surface;
    NodeRig         rig(kRate);
    std::string     why;
    REQUIRE(rig.registry.addPackage(surfaceTestPackage(), &why));
    rig.devices.install(nullptr, &surface);
    // A source writing the output node's out, so the counted block covers both halves of the link: the drain into the
    // input node's `in`, which no probe reads (a probe allocates its copies), and the hand-off of what the source
    // queued.
    plan(rig, surfaceDocument({docNode("out", ScriptedOut::id)}, {docWire("out.out", "surface-out.out")}));

    // Room for the block's work exists before the window: the fake's input and the slots it records in.
    surface.input = {deviceMessage({0x90, 0x51, 0x7F}), deviceMessage({0x90, 0x51, 0x00})};
    block(rig);
    REQUIRE(surface.outputs.size() == 2);
    surface.outputs.clear();
    surface.reserve(0, 2);
    surface.next = 0;
    surface.input = {deviceMessage({0x90, 0x51, 0x7F}), deviceMessage({0x90, 0x51, 0x00})};

    rp::test::beginCountingAllocations();
    block(rig);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(surface.outputs.size() == 2);
    CHECK(rig.host->droppedMessages() == 0);
}
