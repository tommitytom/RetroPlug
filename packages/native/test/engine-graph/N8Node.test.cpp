// rp.n8.nes, the real cart as a node (docs/modular-graph-phase-c5-design.md section 4): the same inputs an emulated NES
// takes, pushed at the host's cart link; a `connected` control output; the `expansionVolume` param on the cart's
// master-volume register; and its battery file and ROM as capability jobs the link runs on its own thread
// (section 6), which pause its streaming.
//
// The cart link here is a fake: it records what reaches the device, reads connected while it is attached and no job
// holds it, drops (and counts) what is pushed otherwise, and lets a case play the device's thread for a job (progress,
// then the result or the error, streaming resumed before the completion). How the real link pauses and resumes is
// retroplug-n8-test's (N8CartJobs.test.cpp). Wiring, frames, order and the param are held on a NodeRig, where a case
// reads the node's neighbours directly; the jobs, a removal and the allocation count on a real Engine.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[n8-node]"`.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "host/devices/DeviceLinks.hpp"
#include "host/engine/CapabilityJobs.hpp"
#include "host/engine/Engine.hpp"
#include "host/graph/N8Nodes.hpp"

namespace {

using rp::host::CartLink;
using rp::host::DeviceJobSlot;
using rp::host::Job;
using rp::host::JobRequest;
using rp::host::test::DocExtras;
using rp::host::test::DocSystem;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::Feed;
using rp::host::test::graphRequest;
using rp::host::test::Message;
using rp::host::test::NodeRig;
using rp::host::test::Probe;
using rp::host::test::stereoDocument;
using Bytes = std::vector<std::uint8_t>;

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 64;

// A cart link a case drives: the device attached or not, a job held or not, and what reached it.
class FakeCart final : public CartLink {
public:
    // A slot of its own, so a recorded push copies bytes and never allocates: reserve before a counted block.
    struct Push {
        std::uint32_t frame = 0;
        bool          flush = false;
        double        rate  = 0.0;
        std::uint16_t size  = 0;
        std::uint8_t  data[rp::host::kMaxDeviceMessage] = {};
        Bytes         bytes() const { return Bytes(data, data + size); }
    };

    bool streaming() const { return attached && !jobRunning; }

    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush, double rate) override {
        if (!streaming()) {  // detached, or a job holds the device: the bytes go nowhere and are counted
            ++drops;
            return false;
        }
        if (refuseAfterPushes == 0) {  // the device's ring was full for this one: the bytes are lost, and counted
            refuseAfterPushes = -1;
            ++drops;
            return false;
        }
        if (refuseAfterPushes > 0) --refuseAfterPushes;
        Push p;
        p.frame = frame;
        p.flush = flush;
        p.rate  = rate;
        p.size  = static_cast<std::uint16_t>(std::min(size, rp::host::kMaxDeviceMessage));
        std::memcpy(p.data, data, p.size);
        pushes.push_back(p);
        return true;
    }
    bool          connected() const override { return streaming(); }
    std::uint64_t dropped() const override { return drops; }
    void          setExpansionVolume(int value) override { volumes.push_back(value); }
    void          acquire() override { ++acquires; }
    void          release() override { ++releases; }

    // One job at a time, none while detached, as the real link answers; a taken job holds the device until the case,
    // playing its thread, finishes it.
    bool submitJob(const JobRequest& request, Job& job, const void* owner, std::string* error) override {
        if (slot.busy()) {
            *error = "busy: the cart is running another job";
            return false;
        }
        if (!attached) {
            *error = "not connected";
            return false;
        }
        if (!slot.claim(job, owner)) return false;
        lastRequest = request;
        jobRunning  = true;
        return true;
    }
    void abandonJobs(const void* owner) override {
        ++abandons;
        slot.abandon(owner);
    }
    // The device's thread: progress, then streaming back and the completion after it.
    void progress(std::uint32_t done, std::uint32_t total) { slot.progress(done, total); }
    void finish(Bytes result) {
        jobRunning = false;
        slot.succeed(std::move(result));
    }
    void failJob(std::string error) {
        jobRunning = false;
        slot.fail(std::move(error));
    }

    bool              attached   = true;
    bool              jobRunning = false;
    int               refuseAfterPushes = -1;  // >= 0: the push after that many more of this block's is refused
    std::uint64_t     drops      = 0;
    int               acquires   = 0;
    int               releases   = 0;
    int               abandons   = 0;
    std::vector<int>  volumes;
    std::vector<Push> pushes;
    JobRequest        lastRequest;
    DeviceJobSlot     slot;
};

void block(NodeRig& rig) { rig.host->processBlock(kFrames, rp_transport{kRate, 120.0, 0.0, 0}); }

// One block of an Engine's stereo pair, into buffers the caller sized (a counted case sizes them first).
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

// A NodeRig document: a feed whose midi and bytes go to a NES and to the cart, as buildGraph fans one system's
// protocol output out to both, and a probe reading the cart's `connected`. `params` are the cart node's param members,
// JSON without braces.
std::string fanOut(NodeRig& rig, const std::string& params = "") {
    return R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"sys-1","type":"rp.mesen.nes","settings":{)" +
           rig.boot("rp.mesen.nes", 1) + R"(}},{"id":"cart","type":"rp.n8.nes")" +
           (params.empty() ? "" : R"(,"params":{)" + params + "}") +
           R"(},{"id":"probe","type":"rp.test.probe"}],
               "wires":[{"from":"feed.midi","to":"sys-1.midi"},{"from":"feed.bytes","to":"sys-1.bytes"},
                        {"from":"feed.midi","to":"cart.midi"},{"from":"feed.bytes","to":"cart.bytes"},
                        {"from":"cart.connected","to":"probe.control"}]})";
}

// The Engine documents: one NES, host MIDI wired to it and to the cart, the cart's params as given.
Engine::GraphRequest cartDocument(std::vector<DocSystem>& systems, const std::string& cartParams = "") {
    const std::string cart = rp::host::test::docs_detail::node("cart", "rp.n8.nes", "", cartParams);
    return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in"), cart},
                                                          {docWire("midi-in.midi", "sys-1.midi"),
                                                           docWire("midi-in.midi", "cart.midi")}}),
                        {1});
}
// The same without the cart, so publishing it removes the node.
Engine::GraphRequest noCartDocument(std::vector<DocSystem>& systems) {
    return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in")},
                                                          {docWire("midi-in.midi", "sys-1.midi")}}),
                        {1});
}

JobRequest request(const char* capability, const char* op) {
    JobRequest r;
    r.nodeId     = "cart";
    r.capability = capability;
    r.op         = op;
    return r;
}

// The completion of job `id` among what a drain returned, or a progress entry for it when `done` is false.
const rp::host::JobCompletion* entry(const std::vector<rp::host::JobCompletion>& drained, std::uint32_t id, bool done) {
    for (const auto& c : drained)
        if (c.id == id && c.done == done) return &c;
    return nullptr;
}

}  // namespace

TEST_CASE("rp.n8.nes receives what rp.mesen.nes receives from one source, in order, at their frames, flush carried",
          "[engine-graph][n8-node]") {
    FakeCart cart;
    NodeRig  rig(kRate);
    rig.devices.install(&cart, nullptr);
    REQUIRE(rig.set(fanOut(rig)).ok);
    Feed& feed = rig.node<Feed>("feed");
    // Several messages at one frame, a SysEx longer than a MidiEvent, an empty one (skipped), and bytes with and
    // without the flush flag.
    feed.next[Feed::Midi]  = {{3, {0x90, 60, 100}}, {3, {0xB0, 7, 90}}, {8, {}}, {8, {0x80, 60, 0}},
                              {20, {0xF0, 0x7D, 0x42, 0x01, 0x00, 0x08, 0xF7}}};
    feed.next[Feed::Bytes] = {{1, {0xF4, 0x00}, RP_MESSAGE_FLUSH}, {5, {0xF8}}, {63, {0xF5, 0x01, 0x02}}};
    block(rig);

    // The cart got the MIDI then the bytes, each in queue order at its frame, only the bytes' flag set, and at the
    // block's rate, which the link times the release at.
    struct Push {
        std::uint32_t frame;
        Bytes         bytes;
        bool          flush;
    };
    const std::vector<Push> expected{{3, {0x90, 60, 100}, false}, {3, {0xB0, 7, 90}, false}, {8, {0x80, 60, 0}, false},
                                     {20, {0xF0, 0x7D, 0x42, 0x01, 0x00, 0x08, 0xF7}, false},
                                     {1, {0xF4, 0x00}, true}, {5, {0xF8}, false}, {63, {0xF5, 0x01, 0x02}, false}};
    // Seven pushes for eight feeds: the empty message at frame 8 is skipped, not pushed.
    REQUIRE(cart.pushes.size() == expected.size());
    for (std::size_t i = 0; i < cart.pushes.size(); ++i) {
        INFO("push " << i);
        CHECK(cart.pushes[i].frame == expected[i].frame);
        CHECK(cart.pushes[i].bytes() == expected[i].bytes);
        CHECK(cart.pushes[i].flush == expected[i].flush);
        CHECK(cart.pushes[i].rate == kRate);
    }
    // And the emulated NES read the same bytes queue: what its bytes input fed its core, as the sync trace saw it.
    REQUIRE(rig.traced.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(rig.traced[i].frame == cart.pushes[4 + i].frame);
        CHECK(rig.traced[i].data == cart.pushes[4 + i].bytes());
    }
    CHECK(cart.drops == 0);
}

TEST_CASE("rp.n8.nes's connected output follows the link from frame 0, and a host with no cart plans and runs it",
          "[engine-graph][n8-node]") {
    SECTION("an installed link, attached, then gone, then back") {
        FakeCart cart;
        NodeRig  rig(kRate);
        rig.devices.install(&cart, nullptr);
        REQUIRE(rig.set(fanOut(rig)).ok);
        Probe& probe = rig.node<Probe>("probe");
        block(rig);
        CHECK(probe.controlAtZero == 1.0);
        cart.attached = false;
        rig.node<Feed>("feed").next[Feed::Midi] = {{0, {0x90, 60, 100}}};
        block(rig);
        CHECK(probe.controlAtZero == 0.0);
        CHECK(cart.drops == 1);  // pushed while gone: nowhere, counted
        cart.attached = true;
        block(rig);
        CHECK(probe.controlAtZero == 1.0);
        CHECK(cart.acquires == 1);
    }
    SECTION("no link installed: the never-connected handle, through a real Engine") {
        Engine                         e(kRate);
        rp::host::test::Roms           roms;
        std::vector<DocSystem>         systems{{1, rp::host::test::kNesType}};
        withRoms(e, systems, roms);
        REQUIRE(e.setGraph(cartDocument(systems)).ok);
        e.stageMidi(0, Bytes{0x90, 60, 100});
        block(e);
        block(e);
        CHECK(e.faultCount() == 0);
        // A job has nowhere to go.
        const auto refused = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump"));
        CHECK(refused.id == 0);
        CHECK(refused.error == "not connected");
    }
}

TEST_CASE("rp.n8.nes gives the link its expansion volume at its first block, on a change, and on every reconnect",
          "[engine-graph][n8-node]") {
    FakeCart cart;
    NodeRig  rig(kRate);
    rig.devices.install(&cart, nullptr);
    REQUIRE(rig.set(fanOut(rig, R"("expansionVolume":50)")).ok);
    block(rig);
    // 50 % of unity, rescaled as the UI's expVolToN8 does: round(50 * 128 / 100).
    REQUIRE(cart.volumes == std::vector<int>{64});
    block(rig);
    CHECK(cart.volumes.size() == 1);  // unchanged: nothing more

    REQUIRE(rig.host->setParam("cart", "expansionVolume", 200.0));
    block(rig);
    CHECK(cart.volumes == std::vector<int>{64, 255});  // 256 clamps to the register's 255

    // Gone and back: the value again on the block the link reads connected, though the param did not move.
    cart.attached = false;
    block(rig);
    CHECK(cart.volumes.size() == 2);
    cart.attached = true;
    block(rig);
    CHECK(cart.volumes == std::vector<int>{64, 255, 255});
    CHECK(rp::host::expansionVolumeRegister(100) == 128);
    CHECK(rp::host::expansionVolumeRegister(0) == 0);
}

TEST_CASE("a note-off the link dropped during a pause goes out at frame 0 of the first block it reads connected",
          "[engine-graph][n8-node]") {
    FakeCart cart;
    NodeRig  rig(kRate);
    rig.devices.install(&cart, nullptr);
    REQUIRE(rig.set(fanOut(rig)).ok);
    Feed& feed = rig.node<Feed>("feed");
    feed.next[Feed::Midi] = {{4, {0x91, 60, 100}}, {5, {0x91, 64, 100}}};
    block(rig);
    REQUIRE(cart.pushes.size() == 2);

    // A job holds the device: the note-off for 60, a note-on for 67 and its own note-off are all dropped.
    cart.jobRunning = true;
    feed.next[Feed::Midi] = {{2, {0x81, 60, 0}}, {3, {0x91, 67, 100}}, {9, {0x91, 67, 0}}};
    block(rig);
    CHECK(cart.pushes.size() == 2);
    CHECK(cart.drops == 3);

    // Streaming back: the one note the console is still holding for want of its note-off (60: 67 never sounded) gets
    // it first, then the block's own traffic.
    cart.jobRunning = false;
    feed.next[Feed::Midi] = {{7, {0x81, 64, 0}}};
    block(rig);
    REQUIRE(cart.pushes.size() == 4);
    CHECK(cart.pushes[2].frame == 0);
    CHECK(cart.pushes[2].bytes() == Bytes{0x81, 60, 0});
    CHECK(cart.pushes[3].frame == 7);
    CHECK(cart.pushes[3].bytes() == Bytes{0x81, 64, 0});
    block(rig);
    CHECK(cart.pushes.size() == 4);  // paid once
}

TEST_CASE("an owed note-off the link refuses part-way stays owed, and a retrigger clears one",
          "[engine-graph][n8-node]") {
    SECTION("a pay the link refuses part-way keeps the rest owed") {
        FakeCart cart;
        NodeRig  rig(kRate);
        rig.devices.install(&cart, nullptr);
        REQUIRE(rig.set(fanOut(rig)).ok);
        Feed& feed = rig.node<Feed>("feed");
        // Two notes the cart is holding, then a pause that drops both their note-offs.
        feed.next[Feed::Midi] = {{1, {0x90, 60, 100}}, {2, {0x90, 64, 100}}};
        block(rig);
        REQUIRE(cart.pushes.size() == 2);
        cart.jobRunning = true;
        feed.next[Feed::Midi] = {{0, {0x80, 60, 0}}, {1, {0x80, 64, 0}}};
        block(rig);
        CHECK(cart.drops == 2);

        // Streaming is back, and the link takes the first owed note-off and refuses the second: the one it refused is
        // still owed, so it is not lost.
        cart.jobRunning        = false;
        cart.refuseAfterPushes = 1;
        feed.next[Feed::Midi]  = {};
        cart.pushes.clear();
        block(rig);
        REQUIRE(cart.pushes.size() == 1);
        CHECK(cart.pushes[0].frame == 0);
        CHECK(cart.pushes[0].bytes() == Bytes{0x80, 60, 0});
        block(rig);
        REQUIRE(cart.pushes.size() == 2);
        CHECK(cart.pushes[1].frame == 0);
        CHECK(cart.pushes[1].bytes() == Bytes{0x80, 64, 0});
        block(rig);
        CHECK(cart.pushes.size() == 2);  // both paid once
    }
    SECTION("a note-on that reaches the cart clears that note's owed note-off") {
        FakeCart cart;
        NodeRig  rig(kRate);
        rig.devices.install(&cart, nullptr);
        REQUIRE(rig.set(fanOut(rig)).ok);
        Feed& feed = rig.node<Feed>("feed");
        feed.next[Feed::Midi] = {{1, {0x90, 60, 100}}};
        block(rig);
        cart.jobRunning = true;
        feed.next[Feed::Midi] = {{0, {0x80, 60, 0}}};
        block(rig);
        CHECK(cart.drops == 1);

        // Back, with the link refusing the owed note-off while the block's own note-on for the same note reaches it:
        // the note is sounding again, so nothing is owed, and the next block must not cut it with a stale note-off.
        cart.jobRunning        = false;
        cart.refuseAfterPushes = 0;
        feed.next[Feed::Midi]  = {{5, {0x90, 60, 100}}};
        cart.pushes.clear();
        block(rig);
        REQUIRE(cart.pushes.size() == 1);
        CHECK(cart.pushes[0].frame == 5);
        CHECK(cart.pushes[0].bytes() == Bytes{0x90, 60, 100});
        feed.next[Feed::Midi] = {};
        block(rig);
        CHECK(cart.pushes.size() == 1);
    }
}

TEST_CASE("rp.n8.nes's jobs: one at a time, none while disconnected, streaming paused for each, progress, completion",
          "[engine-graph][n8-node]") {
    FakeCart               cart;
    Engine                 e(kRate);
    rp::host::test::Roms   roms;
    std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
    withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(cartDocument(systems)).ok);
    block(e);

    // The cart presents the battery and the ROM, not the memory (a cart-memory read is a DMA that can wedge a running
    // game, which stays with the CLI's tools).
    const auto memory = e.submitJob(request(RP_SYSTEM_MEMORY_INTERFACE, "read"));
    CHECK(memory.id == 0);
    CHECK(memory.error == "node cart presents no rp.system.memory/1");
    // An id the engine does not know is refused before the node is asked: a node answers rp.step/1 or rp.link/1 with
    // structs of their own, which must never be taken for a capability.
    for (const char* id : {RP_STEP_INTERFACE, RP_LINK_INTERFACE, "rp.system.video/1"}) {
        const auto unknown = e.submitJob(request(id, "dump"));
        CHECK(unknown.id == 0);
        CHECK(unknown.error == std::string("unknown capability ") + id);
    }

    // Detached: refused at once.
    cart.attached = false;
    const auto detached = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump"));
    CHECK(detached.id == 0);
    CHECK(detached.error == "not connected");
    cart.attached = true;

    // A dump is taken and holds the device; a second job is refused busy.
    const auto dump = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump"));
    REQUIRE(dump.id != 0);
    CHECK(cart.lastRequest.op == "dump");
    CHECK(cart.lastRequest.capability == RP_SYSTEM_SRAM_INTERFACE);
    const auto second = e.submitJob(request(RP_SYSTEM_ROM_INTERFACE, "upload"));
    CHECK(second.id == 0);
    CHECK(second.error.rfind("busy", 0) == 0);
    CHECK(e.jobsOutstanding() == 1);

    // While it runs the node reads disconnected and the link drops what the node pushes, and counts it.
    e.stageMidi(3, Bytes{0x90, 60, 100});
    block(e);
    CHECK(cart.pushes.empty());
    CHECK(cart.drops == 1);
    CHECK(cart.dropped() == 1);

    cart.progress(4096, 65536);
    auto drained = e.drainJobs();
    const auto* running = entry(drained, dump.id, false);
    REQUIRE(running != nullptr);
    CHECK(running->progress == 4096);
    CHECK(running->total == 65536);
    CHECK(entry(drained, dump.id, true) == nullptr);

    // Done: streaming is back before the completion is posted, so the next block reaches the cart.
    cart.finish(Bytes(65536, 0x5A));
    drained = e.drainJobs();
    const auto* done = entry(drained, dump.id, true);
    REQUIRE(done != nullptr);
    CHECK(done->ok);
    CHECK(done->bytes == Bytes(65536, 0x5A));
    e.stageMidi(1, Bytes{0x80, 60, 0});
    block(e);
    REQUIRE(cart.pushes.size() == 1);
    CHECK(cart.pushes[0].frame == 1);

    // A device's failure carries its reason; and the slot is free again for the next.
    const auto restore = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "restore"));
    REQUIRE(restore.id != 0);
    cart.failJob("cart SRAM verify failed (readback != save)");
    drained = e.drainJobs();
    const auto* failed = entry(drained, restore.id, true);
    REQUIRE(failed != nullptr);
    CHECK_FALSE(failed->ok);
    CHECK(failed->error == "cart SRAM verify failed (readback != save)");
    CHECK(e.jobsOutstanding() == 0);
}

TEST_CASE("a cart node removed during a job abandons it: it fails at once, and the device reports into it no more",
          "[engine-graph][n8-node]") {
    FakeCart               cart;
    Engine                 e(kRate);
    rp::host::test::Roms   roms;
    std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
    withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(cartDocument(systems)).ok);
    block(e);

    const auto dump = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump"));
    REQUIRE(dump.id != 0);
    cart.progress(8192, 65536);

    // A plan that keeps the node keeps its job.
    REQUIRE(e.setGraph(cartDocument(systems, R"("expansionVolume":120)")).ok);
    block(e);
    CHECK(cart.abandons == 0);
    CHECK(cart.slot.busy());

    // Its removal: the node gives the handle back and abandons the job, which fails as the node's other jobs do.
    REQUIRE(e.setGraph(noCartDocument(systems)).ok);
    block(e);
    block(e);  // the plan that held it is reclaimed here at the latest
    CHECK(cart.abandons == 1);
    CHECK(cart.releases == 1);
    auto drained = e.drainJobs();
    const auto* removed = entry(drained, dump.id, true);
    REQUIRE(removed != nullptr);
    CHECK_FALSE(removed->ok);
    CHECK(removed->error == "node cart left the plan before the job completed");
    CHECK(e.jobsOutstanding() == 0);

    // The device is still busy finishing; what it reports now reaches no record (the engine may have freed it), and
    // only its finishing frees the device for the next job.
    CHECK(cart.slot.busy());
    cart.progress(65536, 65536);
    cart.finish(Bytes(16, 1));
    CHECK_FALSE(cart.slot.busy());
    CHECK(e.drainJobs().empty());
}

TEST_CASE("a warmed block feeding a NES and its cart node allocates and frees nothing", "[engine-graph][n8-node]") {
    FakeCart               cart;
    Engine                 e(kRate);
    rp::host::test::Roms   roms;
    std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
    withRoms(e, systems, roms);
    e.setDeviceLinks(&cart, nullptr);
    REQUIRE(e.setGraph(cartDocument(systems, R"("expansionVolume":80)")).ok);
    for (int i = 0; i < 4; ++i) block(e);
    // A note the cart holds and whose note-off a pause then drops, so the counted block pays an owed note-off too.
    e.stageMidi(0, Bytes{0x90, 60, 100});
    block(e);
    cart.jobRunning = true;
    e.stageMidi(0, Bytes{0x80, 60, 0});
    block(e);
    cart.jobRunning = false;
    REQUIRE(e.setParam("cart", "expansionVolume", 90.0));
    e.stageMidi(5, Bytes{0x90, 62, 100});
    e.stageMidi(9, Bytes{0x80, 62, 0});
    cart.pushes.clear();
    cart.pushes.reserve(8);
    cart.volumes.reserve(8);

    std::vector<float> l(kFrames), r(kFrames);  // the block's own output, sized before the window
    rp::test::beginCountingAllocations();
    block(e, l, r);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    REQUIRE(cart.pushes.size() == 3);  // the owed note-off, then the block's two
    CHECK(cart.pushes[0].bytes() == Bytes{0x80, 60, 0});
    CHECK(cart.volumes.back() == rp::host::expansionVolumeRegister(90));
}

TEST_CASE("rp.n8.nes declares threads, so a target with none refuses the type", "[engine-graph][n8-node]") {
    const rp_package* pkg = rp::host::n8Package();
    const rp_node_type* cart = nullptr;
    for (std::uint32_t i = 0; i < pkg->node_type_count; ++i)
        if (std::string(pkg->node_types[i]->id) == "rp.n8.nes") cart = pkg->node_types[i];
    REQUIRE(cart != nullptr);
    CHECK(std::string(cart->descriptor(cart)).find(R"("threads":true)") != std::string::npos);

    NodeRig pico(kRate, 8192, rp::graph::Target::Pico);
    const auto refused = pico.set(R"({"nodes":[{"id":"cart","type":"rp.n8.nes"}],"wires":[]})");
    REQUIRE_FALSE(refused.ok);
    CHECK(refused.errors.at(0).code == "capability");
    CHECK(refused.errors.at(0).message.find("threads") != std::string::npos);
    NodeRig desktop(kRate);
    CHECK(desktop.set(R"({"nodes":[{"id":"cart","type":"rp.n8.nes"}],"wires":[]})").ok);
}
