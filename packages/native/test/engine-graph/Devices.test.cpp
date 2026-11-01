// The host's device links, rp.host.devices/1 (docs/modular-graph-phase-c5-design.md sections 3 to 5): a node binds the
// hardware it drives by name and is handed a link, which it borrows for the life of its plan and gives back when the
// plan's reclaim destroys it. Driven on a real Engine with a test node that does what rp.n8.nes and the surface nodes
// do: it acquires both links in its activate, pushes at the cart's frame offsets in its block, drains the surface into
// storage it owns and pushes one LED message back, and releases both in its destructor.
//
// The links here are fakes that record what a node did with them, and count how many nodes hold them: what reaches the
// device, its order and frames, the connected state a host with no device leaves in place, the balance of acquire and
// release across a plan replaced and a node removed, and a warmed block that allocates and frees nothing.
//
// Run via `pnpm test:plugin engine-graph`.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "FakeSurfaceLink.hpp"
#include "GraphDocs.hpp"
#include "graph/node.hpp"
#include "host/devices/DeviceLinks.hpp"
#include "host/engine/Engine.hpp"
#include "host/graph/HostServices.hpp"

namespace {

using rp::host::CartLink;
using rp::host::DeviceMessage;
using rp::host::HostDevices;
using rp::host::SurfaceLink;
using rp::host::test::deviceMessage;
using rp::host::test::FakeSurfaceLink;

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 64;

// One message the probe pushes at the cart, scripted by the test.
struct CartPush {
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
    bool                      flush = false;
};

// What the probe node reads and writes: a document cannot carry a pointer, and the engine hands a test no handle on a
// node's instance (NodeRig does), so the probe's script and what it saw live here. One case at a time, as the suite
// runs. `process` only reads the vector and writes the fixed members, so a counted block allocates nothing.
struct ProbeScript {
    std::vector<CartPush>         cart;          // pushed on the next block, in order
    std::array<DeviceMessage, 16> drained{};     // what the last block drained from the surface
    std::size_t                   drainedCount = 0;
    double                        rate         = 0.0;      // the rate the last block pushed the cart at
    int                           volume       = -2;       // the expansion register to set, -2 to leave it alone
    std::uint64_t                 cartDropped    = 0;       // the cart link's lost pushes, as the node reads it
    std::uint64_t                 surfaceDropped = 0;       // the surface link's count, read the same way
    bool                          cartConnected    = false;  // what the two handles answer, as the node reads them
    bool                          surfaceConnected = false;
    const char*                   cartName         = rp::host::kCartDeviceName;     // what the node asks for
    const char*                   surfaceName      = rp::host::kSurfaceDeviceName;  // what the node asks for
    bool                          destroyed        = false;  // the node's destroy ran
    std::uint64_t                 droppedAtDestroy = 0;      // a read of the cart's count from the destroy
};
ProbeScript script;

// The message the probe always pushes at the surface, so a case sees the node's own LED traffic beside what it drained.
constexpr std::uint8_t kSurfaceOut[] = {0xB0, 0x10, 0x7F};

// A node that binds both device links, the way the two hardware nodes will: acquire in activate, use in process, give
// the handles back from the destructor that the plan's reclaim runs (on the control thread).
struct DeviceProbe {
    static constexpr const char* id = "rp.test.device-probe";

    const HostDevices*   devices = nullptr;
    CartLink*            cart    = nullptr;
    SurfaceLink*         surface = nullptr;
    std::array<DeviceMessage, 16> in{};  // in the node's own storage, so the drain frees nothing on the block thread

    void bind_host(const rp_host* h) noexcept {
        devices = static_cast<const HostDevices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
    }
    bool activate(double, std::uint32_t) {
        if (!devices) return false;
        // A reactivate (a sample-rate change, a block growth) runs activate again on this same instance: a handle
        // already held is kept, so one acquire here is one release in the destructor.
        if (!cart) cart = devices->acquireCart(devices->ctx, script.cartName);
        if (!surface) surface = devices->acquireSurface(devices->ctx, script.surfaceName);
        return cart != nullptr && surface != nullptr;
    }
    void process(const rp::Block& b) noexcept {
        script.rate = b.transport().sample_rate;
        if (script.volume != -2) cart->setExpansionVolume(script.volume);
        for (const CartPush& p : script.cart)
            cart->push(p.frame, p.data.data(), p.data.size(), p.flush, script.rate);
        surface->pushOutput(kSurfaceOut, sizeof kSurfaceOut);
        const std::size_t n = surface->drainInput(in.data(), in.size());
        script.drainedCount = n;
        for (std::size_t i = 0; i < n; ++i) script.drained[i] = in[i];
        script.cartDropped      = cart->dropped();
        script.surfaceDropped   = surface->dropped();
        script.cartConnected    = cart->connected();
        script.surfaceConnected = surface->connected();
    }
    ~DeviceProbe() {
        if (!devices) return;
        // A host tears its plans down while its links are alive, so a node may use a handle here, not only give it
        // back: this reads the cart's drop count before releasing both.
        script.droppedAtDestroy = cart ? cart->dropped() : 0;
        if (cart) devices->releaseCart(devices->ctx, cart);
        if (surface) devices->releaseSurface(devices->ctx, surface);
        script.destroyed = true;
    }
};

const rp_package* deviceProbePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<DeviceProbe>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.devices"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

// A cart link that records every push and what a node asked of it, and counts the nodes holding it.
class FakeCartLink final : public CartLink {
public:
    // A slot of its own, so a recorded push copies bytes and never allocates: reserve(n) before a counted block.
    struct Push {
        std::uint32_t frame      = 0;
        double        sampleRate = 0.0;
        bool          flush      = false;
        std::uint16_t size       = 0;
        std::uint8_t  data[rp::host::kMaxDeviceMessage] = {};
    };

    void reserve(std::size_t n) { pushes.reserve(n); }

    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
              double sampleRate) override {
        if (!attached) {  // the device went away: the bytes go nowhere, as a real link's do
            ++drops;
            return false;
        }
        Push p;
        p.frame      = frame;
        p.flush      = flush;
        p.sampleRate = sampleRate;
        p.size       = static_cast<std::uint16_t>(std::min(size, rp::host::kMaxDeviceMessage));
        std::memcpy(p.data, data, p.size);
        pushes.push_back(p);
        return true;
    }
    bool          connected() const override { return attached; }
    std::uint64_t dropped() const override { return drops; }
    void          setExpansionVolume(int value) override { volume = value; }
    void          acquire() override {
        ++acquires;
        ++holders;
    }
    void release() override {
        ++releases;
        --holders;
        if (holders < minHolders) minHolders = holders;
    }

    bool              attached = true;
    std::uint64_t     drops    = 0;
    int               volume   = -1;
    int               acquires = 0;
    int               releases = 0;
    // The nodes holding the link right now, and the lowest that count has been after a release: a link that resets its
    // device state on its last release needs the second never to be 0 while anything still drives the cart.
    int               holders    = 0;
    int               minHolders = 1;
    std::vector<Push> pushes;
};

using rp::host::test::DocExtras;
using rp::host::test::docNode;
using rp::host::test::graphRequest;
using rp::host::test::stereoDocument;

// A document holding the probe beside the host's stereo pair, and nothing else.
Engine::GraphRequest probeDocument() {
    return graphRequest(stereoDocument({}, DocExtras{{docNode("probe", DeviceProbe::id)}, {}}), {});
}

// The same document with the probe under another node id: a replacement instance, as a project edit that swaps the
// system driving the cart makes.
Engine::GraphRequest probeDocumentAs(const std::string& id) {
    DocExtras extras;
    extras.nodes.push_back(docNode(id, DeviceProbe::id));
    return graphRequest(stereoDocument({}, extras), {});
}

// The same document without the probe, so publishing it reclaims the plan that held the node.
Engine::GraphRequest noProbeDocument() {
    return graphRequest(stereoDocument({}, DocExtras{{docNode("midi-in", "rp.host.midi-in")}, {}}), {});
}

// One block of the engine's stereo pair, into buffers the caller owns: a counted case sizes them before it starts
// counting, so the block's own output is not what it measures.
void block(Engine& e, std::vector<float>& l, std::vector<float>& r) {
    l.assign(kFrames, 0.0f);
    r.assign(kFrames, 0.0f);
    float* outs[2] = {l.data(), r.data()};
    e.processBlock(kFrames, outs, 2);
}

void block(Engine& e) {
    std::vector<float> l(kFrames), r(kFrames);
    block(e, l, r);
}

}  // namespace

TEST_CASE("a node's cart pushes reach the installed cart link in order, at their frames and flush", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink cart;
    Engine       e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(probeDocument()).ok);

    script.cart = {{0, {0x90, 0x40, 0x7F}, false},
                   {5, {0xF0, 0x7D, 0x01, 0xF7}, true},
                   {63, {0x80, 0x40, 0x00}, false}};
    script.volume = 42;
    block(e);
    script.cart.clear();

    // The node's own LED traffic went to the surface name, which this host has no link for: nothing reached the cart
    // but the three pushes, in order, each carrying the frame the block gave it and its flush flag.
    REQUIRE(cart.pushes.size() == 3);
    CHECK(cart.pushes[0].frame == 0);
    CHECK(cart.pushes[0].flush == false);
    CHECK(std::vector<std::uint8_t>(cart.pushes[0].data, cart.pushes[0].data + cart.pushes[0].size) ==
          std::vector<std::uint8_t>{0x90, 0x40, 0x7F});
    CHECK(cart.pushes[1].frame == 5);
    CHECK(cart.pushes[1].flush == true);
    CHECK(std::vector<std::uint8_t>(cart.pushes[1].data, cart.pushes[1].data + cart.pushes[1].size) ==
          std::vector<std::uint8_t>{0xF0, 0x7D, 0x01, 0xF7});
    CHECK(cart.pushes[2].frame == 63);
    CHECK(std::vector<std::uint8_t>(cart.pushes[2].data, cart.pushes[2].data + cart.pushes[2].size) ==
          std::vector<std::uint8_t>{0x80, 0x40, 0x00});
    // The rate the push's timing is computed at is the block's: the node takes it from the transport, not from its
    // settings.
    CHECK(cart.pushes[0].sampleRate == kRate);
    CHECK(script.rate == kRate);
    // And the registry forwards the expansion volume a node sets through the interface, untouched.
    CHECK(cart.volume == 42);
    CHECK(script.cartConnected);
}

TEST_CASE("the surface link's scripted input arrives in order, and its output is recorded", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeSurfaceLink surface;
    surface.input = {deviceMessage({0x90, 0x51, 0x7F}), deviceMessage({0x90, 0x51, 0x00})};
    Engine e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(nullptr, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);

    block(e);
    REQUIRE(script.drainedCount == 2);
    CHECK(script.drained[0].size == 3);
    CHECK(std::vector<std::uint8_t>(script.drained[0].data, script.drained[0].data + script.drained[0].size) ==
          std::vector<std::uint8_t>{0x90, 0x51, 0x7F});
    CHECK(script.drained[1].size == 3);
    CHECK(std::vector<std::uint8_t>(script.drained[1].data, script.drained[1].data + script.drained[1].size) ==
          std::vector<std::uint8_t>{0x90, 0x51, 0x00});

    // What the node pushed at the device, recorded as it queued it.
    REQUIRE(surface.outputs.size() == 1);
    CHECK(surface.outputs[0].size == sizeof kSurfaceOut);
    CHECK(std::vector<std::uint8_t>(surface.outputs[0].data,
                                    surface.outputs[0].data + surface.outputs[0].size) ==
          std::vector<std::uint8_t>(std::begin(kSurfaceOut), std::end(kSurfaceOut)));

    // Drained once, delivered once: the next block has nothing to take.
    block(e);
    CHECK(script.drainedCount == 0);
    CHECK(surface.outputs.size() == 2);  // the node pushes its LED frame every block, whatever the device sent
}

TEST_CASE("a link whose device goes away reads disconnected and counts what the node pushes", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);

    script.cart = {{0, {0x90, 0x40, 0x7F}, false}, {7, {0x80, 0x40, 0x00}, false}};
    block(e);
    REQUIRE(cart.pushes.size() == 2);

    // Nothing edits the graph when the cart goes: the link says so and swallows the node's pushes, which the node reads
    // back as its device's drop count.
    cart.attached    = false;
    surface.attached = false;
    REQUIRE_FALSE(cart.connected());
    REQUIRE_FALSE(surface.connected());
    block(e);
    CHECK(cart.pushes.size() == 2);
    CHECK(cart.drops == 2);
    CHECK(script.cartDropped == 2);
    CHECK(surface.detached == 1);      // the node's LED frame, which the link took nowhere while the device was gone
    CHECK(surface.dropped() == 1);     // what a node reads: this fake has no ring, so a detached push is the only loss
    CHECK(surface.outputs.size() == 1);
}

TEST_CASE("a host with no device still plans and runs the node, whose pushes go nowhere and are counted", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    // No setDeviceLinks at all, as in the CLI, the Node addon, the render host and the UI harness: the names answer
    // with a handle that is never connected, never null, so the node needs no null check and this document is the same
    // one every host plans.
    Engine e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    REQUIRE(e.setGraph(probeDocument()).ok);

    script.cart = {{0, {0x90, 0x40, 0x7F}, false}, {7, {0x80, 0x40, 0x00}, false}};
    script.volume = 42;  // through the fallback's setExpansionVolume too: a no-op, and no fault
    block(e);
    block(e);
    script.cart.clear();

    CHECK(e.faultCount() == 0);
    CHECK(script.rate == kRate);  // the node ran: its block reached the device links it holds
    CHECK(script.drainedCount == 0);
    // Neither handle is ever connected, so a node reading the device's state takes it as absent, and a volume it sets
    // goes nowhere without a word.
    CHECK_FALSE(script.cartConnected);
    CHECK_FALSE(script.surfaceConnected);
    // A never-connected handle accepts every push and counts it as dropped, which is the count a node reads off its
    // device: two blocks, two pushes each.
    CHECK(script.cartDropped == 4);
    CHECK(script.surfaceDropped == 2);
}

TEST_CASE("acquire and release balance across a plan that keeps the node and one that removes it", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);

    REQUIRE(e.setGraph(probeDocument()).ok);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 0);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 0);

    // A replace that keeps the node: the plan is republished with the same document, and the node runs on with the
    // handles it acquired, so nothing is acquired or released again. Both are counted: a destroy-then-recreate would
    // also leave one holder, and would show up here as two of each.
    REQUIRE(e.setGraph(probeDocument()).ok);
    block(e);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 0);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 0);

    // A removal: the plan that held the node is reclaimed, and its destructor gives both handles back.
    REQUIRE(e.setGraph(noProbeDocument()).ok);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 1);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 1);

    // And a node added back takes both again.
    REQUIRE(e.setGraph(probeDocument()).ok);
    CHECK(cart.acquires == 2);
    CHECK(cart.releases == 1);
    CHECK(surface.acquires == 2);
    CHECK(surface.releases == 1);
}

TEST_CASE("a handover to a new node never leaves the cart link with no holder", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink cart;
    Engine       e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, nullptr);

    // The document is republished with the node replaced by a new instance under a new id - what a project edit that
    // swaps the system driving the cart does. The plan that replaces the first activates the second (and so acquires)
    // before the first is reclaimed and destroyed, so the count a last release keys on never reaches zero.
    REQUIRE(e.setGraph(probeDocumentAs("cart-a")).ok);
    CHECK(cart.holders == 1);
    REQUIRE(e.setGraph(probeDocumentAs("cart-b")).ok);
    CHECK(cart.acquires == 2);
    CHECK(cart.releases == 1);  // with no audio thread the old plan is reclaimed at the publish: its node released
    CHECK(cart.holders == 1);
    CHECK(cart.minHolders >= 1);  // ... after the new one acquired, so that release left a holder
    block(e);
    CHECK(script.rate == kRate);  // the new node runs on the handle it took

    // Removing it is the only release that empties the link, and it comes once for each node that held it: the
    // handover's own releases each left a holder behind.
    REQUIRE(e.setGraph(noProbeDocument()).ok);
    CHECK(cart.releases == 2);
    CHECK(cart.holders == 0);
    CHECK(cart.minHolders == 0);  // the count this link can key a device reset on, reached only with nothing left
}

TEST_CASE("a reactivate keeps the handles a node already holds, and destroy still releases once", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);
    REQUIRE(cart.acquires == 1);
    REQUIRE(surface.acquires == 1);

    // A sample-rate change deactivates and activates every instance again (GraphHost::reactivate, which is also what
    // a block growth goes through). The node keeps the handles it holds, so neither link is acquired a second time.
    e.setSampleRate(44100.0);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 0);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 0);

    // A block growth is the other reactivate path.
    e.setMaxBlockFrames(Engine::kDefaultMaxBlockFrames + 1);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 0);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 0);

    // The node still runs on those handles, at the new rate.
    block(e);
    CHECK(script.rate == 44100.0);
    REQUIRE(cart.pushes.size() == 0);

    // And removing it releases each exactly once.
    REQUIRE(e.setGraph(noProbeDocument()).ok);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 1);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 1);
}

TEST_CASE("a cart push carries the block's rate, not a fixed one", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink cart;
    Engine       e(44100.0);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(probeDocument()).ok);

    script.cart = {{3, {0x90, 0x40, 0x7F}, false}};
    block(e);
    REQUIRE(cart.pushes.size() == 1);
    CHECK(cart.pushes[0].sampleRate == 44100.0);
    CHECK(script.rate == 44100.0);
}

TEST_CASE("a device name this host has no link for answers with a never-connected handle", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    // The host's cart is named "n8" (kCartDeviceName); the node asks for another name, so it gets the registry's
    // never-connected fallback and the installed link is never touched. One behaviour for a typo and for a device the
    // host does not own, so a node needs no null check and a document plans wherever it runs.
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    script.cartName = "nes";
    REQUIRE(e.setGraph(probeDocument()).ok);

    CHECK(cart.acquires == 0);  // the installed cart was not handed out
    CHECK(surface.acquires == 1);  // the surface's name was right

    script.cart = {{0, {0x90, 0x40, 0x7F}, false}};
    block(e);
    CHECK(cart.pushes.empty());
    CHECK(script.cartDropped == 1);  // the fallback counted the push it took nowhere
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a release after the host clears its links reaches nothing and is harmless", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);
    REQUIRE(cart.acquires == 1);

    // A host taking its links back before the objects they wrap go: the plans still alive hand their handles to a
    // registry that no longer names either link, so nothing is forwarded to a link that could already be gone.
    e.setDeviceLinks(nullptr, nullptr);
    REQUIRE(e.setGraph(noProbeDocument()).ok);
    CHECK(cart.acquires == 1);
    CHECK(cart.releases == 0);
    CHECK(surface.acquires == 1);
    CHECK(surface.releases == 0);
}

TEST_CASE("a host's teardown destroys its plans while the links are still alive", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);
    REQUIRE(cart.acquires == 1);

    // What a host that owns both links does in its destructor body, before its device members go: reclaim the plan, so
    // every node's destroy runs while the link it borrowed is still alive (it reads the cart's count there).
    e.clearGraph();
    CHECK(script.destroyed);
    CHECK(script.droppedAtDestroy == 0);
    CHECK(cart.releases == 1);
    CHECK(surface.releases == 1);

    // Only then do the links leave the registry, and a later release nothing is left to reach.
    e.setDeviceLinks(nullptr, nullptr);
    CHECK(cart.releases == 1);
    CHECK(surface.releases == 1);
}

TEST_CASE("a warmed block touching both device links allocates and frees nothing", "[devices]") {
    script = ProbeScript{};  // one case at a time: nothing a case scripted reaches the next
    FakeCartLink    cart;
    FakeSurfaceLink surface;
    Engine          e(kRate);
    REQUIRE(e.addNodePackage(deviceProbePackage()));
    e.setDeviceLinks(&cart, &surface);
    REQUIRE(e.setGraph(probeDocument()).ok);

    // One block first, so every plan-side buffer exists, then the script and the fakes' room for the block's work,
    // which a counted block must not grow.
    script.cart = {{0, {0x90, 0x40, 0x7F}, false}, {4, {0xF0, 0x7D, 0x01, 0xF7}, true}};
    surface.input = {deviceMessage({0x90, 0x51, 0x7F}), deviceMessage({0x90, 0x51, 0x00})};
    block(e);
    cart.pushes.clear();
    cart.reserve(2);
    surface.outputs.clear();
    surface.reserve(0, 1);
    surface.next = 0;
    surface.input = {deviceMessage({0x90, 0x51, 0x7F}), deviceMessage({0x90, 0x51, 0x00})};

    std::vector<float> l(kFrames), r(kFrames);  // the block's own output, sized before the window
    rp::test::beginCountingAllocations();
    block(e, l, r);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(cart.pushes.size() == 2);
    CHECK(script.drainedCount == 2);
}
