// Staging a message at ONE node of the running graph (docs/modular-graph-phase-c5-design.md section 4): the command
// names a node by the id its document gives it, and the engine hands the bytes to the node the plan RUNNING at that
// point of the drain places under it, through rp.stage/1 - a system node's core ingress, a device node's device. So the
// UI's settings message reaches the cart whose menu it is whatever the project's musical routing does with channels,
// and the engine reads no node's type: it asks by an interface id and never by a type name.
//
// What is held here is the delivery: the emulated NES's core accepts the bytes the engine stages at its node, the fake
// cart records the push a stage at its node makes (at frame 0, with the block's rate), an id the running plan does not
// place takes nothing, and the id resolves against the plan that runs rather than the one that ran when the command was
// pushed. The RPC that pushes one is the TS harness's (test-native/stage-node-midi.test.ts, end to end).
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[stage]"`.

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "host/devices/DeviceLinks.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/N8Nodes.hpp"
#include "system/mesen/MesenNesSystem.hpp"

namespace {

using rp::host::test::docNode;
using rp::host::test::DocExtras;
using rp::host::test::DocSystem;
using rp::host::test::docWire;
using rp::host::test::graphRequest;
using rp::host::test::kNesType;
using rp::host::test::stereoDocument;
using Bytes = std::vector<std::uint8_t>;

constexpr double kRate = 48000.0;

// A cart link that records what reaches the device: a stage at the cart's node is one push.
class FakeCart final : public rp::host::CartLink {
public:
    struct Push {
        std::uint32_t frame = 0;
        bool          flush = false;
        double        rate  = 0.0;
        Bytes         data;
    };
    void push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush, double rate) override {
        pushes.push_back({frame, flush, rate, Bytes(data, data + size)});
    }
    bool          connected() const override { return true; }
    std::uint64_t dropped() const override { return 0; }
    void          setExpansionVolume(int) override {}

    std::vector<Push> pushes;
};

// The document: a NES at sys-1 and, unless `cart` is false, an rp.n8.nes at `cart`, with nothing wired between them
// (a stage needs no wire at all: that is the point of it).
Engine::GraphRequest document(std::vector<DocSystem>& systems, bool cart = true) {
    DocExtras extras{};
    if (cart) extras.nodes.push_back(docNode("cart", "rp.n8.nes"));
    extras.nodes.push_back(docNode("midi-in", "rp.host.midi-in"));
    return graphRequest(stereoDocument(systems, extras), {1});
}

// A settings SysEx, longer than four bytes: the shape the UI's rows send, and the one the ring carries as an owning
// payload.
const Bytes kSysEx{0xF0, 0x7D, 0x42, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0xF7};

}  // namespace

TEST_CASE("a staged message reaches the node the running plan places under its id", "[engine-graph][stage]") {
    FakeCart         cart;
    Engine           e(kRate);
    QueuedInvoker    invoker{e};
    rp::host::test::Roms roms;
    std::vector<DocSystem> systems{{1, kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(document(systems)).ok);

    // The cart's node: the link records the message as a frame-0 push at the engine's rate, exactly as its `midi` input
    // would have carried it in the block.
    invoker.stageNodeMidi("cart", kSysEx);
    invoker.drainInto(e);
    REQUIRE(cart.pushes.size() == 1);
    CHECK(cart.pushes[0].frame == 0);
    CHECK(cart.pushes[0].data == kSysEx);
    CHECK_FALSE(cart.pushes[0].flush);
    CHECK(cart.pushes[0].rate == kRate);

    // The system's node: the emulated NES's FIFO ingress accepts the same bytes, uncapped, as the engine's per-system
    // staging did (rp.stage/1's other implementation).
    auto* core = static_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(core != nullptr);
    const std::uint64_t accepted = core->acceptedBytes();
    invoker.stageNodeMidi("sys-1", kSysEx);
    invoker.drainInto(e);
    CHECK(core->acceptedBytes() - accepted == kSysEx.size());
    CHECK(cart.pushes.size() == 1);  // the system's node is not the cart's

    // An id the running plan does not place takes nothing, and neither does a node whose type stages nothing (the host's
    // MIDI input answers no rp.stage/1). Both are silent: the command's contract is optimistic, like pressButton's.
    invoker.stageNodeMidi("nope", kSysEx);
    invoker.stageNodeMidi("midi-in", kSysEx);
    invoker.drainInto(e);
    CHECK(cart.pushes.size() == 1);
    CHECK(core->acceptedBytes() - accepted == kSysEx.size());

    // An empty message is refused before it is ever pushed (`EngineRpcService::stageNodeMidi` refuses it the same way).
    invoker.stageNodeMidi("cart", {});
    invoker.drainInto(e);
    CHECK(cart.pushes.size() == 1);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a node the plan adds later under the same id takes a stored message", "[engine-graph][stage]") {
    FakeCart                cart;
    Engine                  e(kRate);
    QueuedInvoker           invoker{e};
    rp::host::test::Roms    roms;
    std::vector<DocSystem>  systems{{1, kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    // A plan with no cart: staging at "cart" has nowhere to go.
    REQUIRE(e.setGraph(document(systems, false)).ok);
    invoker.stageNodeMidi("cart", kSysEx);
    invoker.drainInto(e);
    CHECK(cart.pushes.empty());
    // The cart's node from here on, under the same id: the same command lands, so the id is resolved at the drain.
    REQUIRE(e.setGraph(document(systems, true)).ok);
    invoker.stageNodeMidi("cart", kSysEx);
    invoker.drainInto(e);
    REQUIRE(cart.pushes.size() == 1);
    CHECK(cart.pushes[0].data == kSysEx);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a staged id resolves against the plan that runs, not the one that ran when it was pushed",
          "[engine-graph][stage]") {
    FakeCart                cart;
    Engine                  e(kRate);
    QueuedInvoker           invoker{e};
    rp::host::test::Roms    roms;
    std::vector<DocSystem>  systems{{1, kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    // The plan that runs places the cart's node.
    REQUIRE(e.setGraph(document(systems, true)).ok);
    // The audio thread owns the drain, so nothing applies inline: the swap and the message wait in the ring.
    invoker.setAudioThreadOwns(true);
    // A plan without the cart is published while the old one still runs, then the message is staged: the swap is
    // ahead of it, so at the push "cart" IS a node of the plan that runs and at the drain it no longer is. The id is
    // resolved there, against the plan just adopted, so the node that ran when the command was pushed takes nothing.
    REQUIRE(e.setGraph(document(systems, false)).ok);
    invoker.stageNodeMidi("cart", kSysEx);
    invoker.drainInto(e);
    CHECK(cart.pushes.empty());
    CHECK(e.faultCount() == 0);
    invoker.setAudioThreadOwns(false);
}
