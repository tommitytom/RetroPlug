// Guards the control-surface transport (src/launchpad/desktop/) without hardware, ported from feature/launchpad's
// Controller.test.cpp onto this pack's fixed-slot link (docs/modular-graph-phase-f3-design.md sections 3 and 8). A fake
// MIDI backend records everything written and hands back each input's receiver, so the whole lifecycle - the claim,
// both rings, the close bytes, the loose ports a scan uses, the provider and its facet - runs with no rtmidi, no MIDI
// system and nothing plugged in.
//
// The close-byte cases matter most. A Launchpad in Programmer mode has its own Settings menu locked until it is let go,
// so a host that closes its port without replaying what TS handed it strands the user's hardware. It has to go out on
// BOTH paths: an explicit disconnect, and destruction.
//
// Nothing here knows any device protocol, and that is the point of the layer: TS builds and parses every byte, so these
// tests assert on ROUTING (which port was written to, which answered, in what order) and on the claim's generation,
// never on what any byte means. The case that drives three threads at once ("a swap mid-drain") is the pack's
// ThreadSanitizer leg (tools/run-sanitizer.sh's launchpad run, and `tsanlp` in the prototype's own verification).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "CountingAllocator.hpp"
#include "graph/rp_provider.h"
#include "launchpad/SurfaceLink.hpp"
#include "launchpad/desktop/ControllerLink.hpp"
#include "launchpad/desktop/LaunchpadProvider.hpp"
#include "launchpad/desktop/LaunchpadRpc.hpp"
#include "launchpad/desktop/MidiPortPool.hpp"
#include "launchpad/desktop/PortClaim.hpp"

#include "FakePorts.hpp"

namespace {

using rp::launchpad::ControllerLink;
using rp::launchpad::DeviceMessage;
using rp::launchpad::LaunchpadPortsDto;
using rp::launchpad::LaunchpadRpc;
using rp::launchpad::LaunchpadStatusDto;
using rp::launchpad::MidiPortOpener;
using rp::launchpad::MidiPortPool;
using rp::launchpad::PortClaim;
using rp::launchpad::kMaxDeviceMessage;
using rp::launchpad::test::Bytes;
using rp::launchpad::test::FakeHost;
using rp::launchpad::test::openerFor;
using rp::launchpad::test::Rig;
using rp::launchpad::test::kCloseBytes;
using rp::launchpad::test::kOtherClose;

/** What one drain answers, as the strings a test compares: the node-facing slots copied out of the link. */
std::vector<Bytes> drained(ControllerLink& link) {
    std::vector<Bytes> out;
    DeviceMessage      slots[64];
    for (;;) {
        const std::size_t n = link.drainInput(slots, std::size(slots));
        for (std::size_t i = 0; i < n; ++i) out.emplace_back(slots[i].data, slots[i].data + slots[i].size);
        if (n < std::size(slots)) break;
    }
    return out;
}

/** rfl::Bytestring for the facet's calls, from the bytes a test spells. */
rfl::Bytestring bytes(std::initializer_list<std::uint8_t> values) {
    const std::vector<std::uint8_t> v(values);
    const auto* at = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(at, at + v.size());
}
rfl::Bytestring bytes(const Bytes& v) {
    const auto* at = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(at, at + v.size());
}

/** Push `bytes` from the audio thread's side. */
void push(ControllerLink& link, const Bytes& bytes) { link.pushOutput(bytes.data(), bytes.size()); }

/** A scratch config dir, removed when it goes: the provider puts its cross-process claims under
 *  <configDir>/locks, and a claim is a file lock, so a facet that claims must be given a REAL directory. The member is
 *  first so the host's config_dir string outlives the provider's use of it. */
struct ScratchDir {
    std::filesystem::path path;

    ScratchDir() {
        static std::atomic<int> counter{0};
        path = std::filesystem::temp_directory_path() /
               ("rp-launchpad-claim-test-" + std::to_string(counter.fetch_add(1)));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
};

/** The ports the production lister would report for these cases: every name a case claims or opens, so the presence
 *  check passes for them. "Gone in"/"Gone out" are listed and then REFUSED by the opener, which is the open that fails
 *  rather than the pick that is stale. */
LaunchpadPortsDto widePorts() {
    return LaunchpadPortsDto{{"Arturia KeyStep 32", "LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "LP in", "A in", "B in",
                              "Taken in", "Gone in", "Scan in", "Chatty in", "A"},
                             {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "LP out", "A out", "B out", "Taken out",
                              "Gone out", "Scan out", "Scan in"}};
}

/** A provider's facet and the host it was created over, from a rig: the two entries that make one a product's. */
struct Facet {
    ScratchDir    dir;
    FakeHost      host;
    void*         self = nullptr;
    LaunchpadRpc* rpc = nullptr;
    Rig&          rig;

    explicit Facet(Rig& r) : host(dir.str()), rig(r) {}
    ~Facet() {
        if (self) rp_launchpad_provider()->destroy(self);
    }
    LaunchpadRpc& start() {
        const rp_provider* provider = rp_launchpad_provider();
        REQUIRE(provider != nullptr);
        auto opener = openerFor(rig);
        self        = rp::launchpad::createProvider(&host.table, std::move(opener), widePorts);
        REQUIRE(self != nullptr);
        rpc = static_cast<LaunchpadRpc*>(provider->rpc_service(self));
        REQUIRE(rpc != nullptr);
        return *rpc;
    }
};

// A stand-in for the products' rpcpp TypedRpcServer: every addMethod it is handed is one mounted method, which is what
// registerRpc promises without needing rpcpp or QuickJS here (the products mount the same template on the real server).
struct CountingServer {
    int methods = 0;
    template <auto M, class Obj>
    void addMethod(Obj&) {
        ++methods;
    }
};

}  // namespace

// ---- ControllerLink: the claim, and the goodbye --------------------------------------------------------------

TEST_CASE("ControllerLink.connect claims the named pair, disconnect gives it back", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", kCloseBytes));
    CHECK(link.isConnected());
    CHECK(link.generation() == 1);
    // Every message in storage the caller owns: nothing is held past a connect.
    CHECK(link.inputName() == "LP in");
    CHECK(link.outputName() == "LP out");
    CHECK(link.lastError().empty());

    link.disconnect();
    CHECK_FALSE(link.isConnected());
    CHECK(link.generation() == 0);
    // The input port is closed, and the goodbye is the last thing the device heard.
    CHECK(rig.listening.empty());
    REQUIRE(rig.written["LP out"].size() == 1);
    CHECK(rig.written["LP out"][0] == kCloseBytes);
}

TEST_CASE("ControllerLink surfaces a refused port, and holds NEITHER half", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    rig.refuse.insert("LP in");
    REQUIRE_FALSE(link.connect("LP in", "LP out", kCloseBytes));
    CHECK_FALSE(link.isConnected());
    CHECK(link.generation() == 0);
    CHECK(link.lastError() == "MIDI port not found: LP in");
    // The output opened before the input and had to be given back, or the device's port stays taken by a link that
    // holds nothing.
    CHECK(rig.listening.empty());
    CHECK(rig.written.empty());
}

TEST_CASE("ControllerLink replays the close bytes on disconnect, once", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", kCloseBytes));
    link.disconnect();
    link.disconnect();  // idempotent: nothing held, nothing said twice
    CHECK(rig.written["LP out"] == std::vector<Bytes>{kCloseBytes});
}

TEST_CASE("ControllerLink replays the close bytes on DESTRUCT too", "[launchpad][link]") {
    Rig rig;
    {
        ControllerLink link(openerFor(rig), false);
        REQUIRE(link.connect("LP in", "LP out", kCloseBytes));
    }
    CHECK(rig.written["LP out"] == std::vector<Bytes>{kCloseBytes});
    CHECK(rig.listening.empty());
}

TEST_CASE("ControllerLink: a reconnect says goodbye to the OLD device with the OLD bytes", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("A in", "A out", kCloseBytes));
    REQUIRE(link.connect("B in", "B out", kOtherClose));
    CHECK(rig.written["A out"] == std::vector<Bytes>{kCloseBytes});
    CHECK(rig.written["B out"].empty());
    CHECK(link.generation() == 2);  // a new claim, never the number a held one had
    link.disconnect();
    CHECK(rig.written["B out"] == std::vector<Bytes>{kOtherClose});
}

// ---- ControllerLink: the rings --------------------------------------------------------------------------------

TEST_CASE("ControllerLink: a message from the device reaches the audio-thread drain, in order", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    rig.deliver("LP in", {0x90, 0x0B, 0x7F});
    rig.deliver("LP in", {0xF0, 0x7E, 0xF7});
    CHECK(drained(link) == std::vector<Bytes>{{0x90, 0x0B, 0x7F}, {0xF0, 0x7E, 0xF7}});
    CHECK(drained(link).empty());  // once, not twice
}

TEST_CASE("ControllerLink copies at most what the caller's slots hold, and keeps the rest", "[launchpad][link]") {
    // A block drains in batches; a message the caller has no room for stays queued rather than being dropped or freed
    // on the audio thread.
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    for (int i = 0; i < 5; ++i) rig.deliver("LP in", {0x90, static_cast<std::uint8_t>(i), 0x7F});
    DeviceMessage two[2];
    CHECK(link.drainInput(two, 2) == 2);
    CHECK(two[0].data[1] == 0);
    CHECK(two[1].data[1] == 1);
    DeviceMessage rest[8];
    CHECK(link.drainInput(rest, 8) == 3);
    CHECK(rest[2].data[1] == 4);
}

TEST_CASE("ControllerLink: queued output goes out on pump, not on push", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    push(link, {0xB0, 0x10, 0x7F});
    CHECK(rig.written["LP out"].empty());
    link.pump();
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xB0, 0x10, 0x7F}});
    CHECK(link.messagesSent() == 1);
}

TEST_CASE("ControllerLink flushes what was queued BEFORE the close bytes", "[launchpad][link]") {
    // The device is not left half-painted: the last block's frames go out, then the goodbye.
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", kCloseBytes));
    push(link, {0xB0, 0x10, 0x01});
    link.disconnect();
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xB0, 0x10, 0x01}, kCloseBytes});
}

TEST_CASE("ControllerLink drops rather than overruns an oversized message", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    const Bytes wide(kMaxDeviceMessage + 1, 0x22);
    push(link, wide);
    link.pump();
    CHECK(rig.written["LP out"].empty());
    CHECK(link.dropped() == 1);
    // The widest the link carries goes through untruncated.
    push(link, Bytes(kMaxDeviceMessage, 0x33));
    link.pump();
    REQUIRE(rig.written["LP out"].size() == 1);
    CHECK(rig.written["LP out"][0].size() == kMaxDeviceMessage);
}

TEST_CASE("ControllerLink ignores output while disconnected, and counts it", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    push(link, {0x90, 0x01, 0x01});  // nothing held
    CHECK(link.dropped() == 1);
    // The paint a session kept pushing while the device was unplugged is what a repaint replaces, so it is counted: a
    // session that sees the count move repaints when one appears.
    CHECK(rig.written.empty());
}

TEST_CASE("ControllerLink drops an oversized message FROM the device, and counts it", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    rig.deliver("LP in", Bytes(kMaxDeviceMessage + 1, 0xF0));  // a patch dump from a synth picked by mistake
    CHECK(drained(link).empty());
    CHECK(link.dropped() == 1);
}

TEST_CASE("ControllerLink: send() goes out after what the session already queued", "[launchpad][link]") {
    // Letting the device go while the pair stays claimed: the LED frame must land before the goodbye, not after it.
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    push(link, {0xB0, 0x10, 0x02});
    const Bytes farewell{0xF0, 0x00, 0xF7};
    CHECK(link.send(farewell.data(), farewell.size()));
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xB0, 0x10, 0x02}, farewell});
    link.disconnect();
    CHECK_FALSE(link.send(farewell.data(), farewell.size()));  // nothing held
}

TEST_CASE("ControllerLink's writer thread delivers what is pushed, and disconnect flushes it", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), true);  // the provider's shape: its own thread
    REQUIRE(link.connect("LP in", "LP out", kCloseBytes));
    push(link, {0xB0, 0x11, 0x03});
    // The writer waits up to 50 ms for a pump; one pump is the frame the session's output is written on. The test polls
    // the link's own atomic count, never the fake's record: the writer thread is what writes that record.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (link.messagesSent() == 0 && std::chrono::steady_clock::now() < deadline) link.pump();
    REQUIRE(link.messagesSent() == 1);
    link.disconnect();  // stops the writer (joined) before it flushes the goodbye
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xB0, 0x11, 0x03}, kCloseBytes});
}

TEST_CASE("the audio path allocates nothing: a drain, a push, the generation and the lost count", "[launchpad][link]") {
    // The block thread runs these four calls every block; an allocation here is a block-thread malloc, which the
    // engine's own cases refuse for the nodes. The link keeps every message in a slot of its own and the node copies into
    // storage it owns, so nothing is allocated or freed - warmed first, since a port's open may allocate.
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    rig.deliver("LP in", {0x90, 0x0B, 0x7F});
    DeviceMessage slots[8];
    while (link.drainInput(slots, std::size(slots)) > 0) {}  // warm: the ring, the batch and whatever the fake touched
    link.pushOutput(Bytes{0xB0, 0x10, 0x01}.data(), 3);
    link.pump();

    rig.deliver("LP in", {0x90, 0x0C, 0x7F});
    rig.deliver("LP in", {0x90, 0x0D, 0x7F});
    const Bytes paint{0xB0, 0x10, 0x02};  // built OUTSIDE the window: only the four calls below are measured
    rp::test::beginCountingAllocations(false);  // operator new alone: nothing in this path may call the C library
    const std::size_t drained = link.drainInput(slots, 1);
    const std::size_t rest    = link.drainInput(slots, std::size(slots));
    link.pushOutput(paint.data(), paint.size());
    const std::uint32_t generation = link.generation();
    const std::uint64_t dropped    = link.dropped();
    const auto          count      = rp::test::endCountingAllocations();
    CHECK(drained == 1);
    CHECK(rest == 1);
    CHECK(generation != 0);
    CHECK(dropped == 0);
    CHECK(count.allocations == 0);
    CHECK(count.frees == 0);
}

TEST_CASE("the provider's per-frame wake writes what the audio thread queued, without waiting its timeout",
          "[launchpad][provider]") {
    // WinMM blocks ~170 ms on a bulk SysEx, which is why the writer is its own thread; the frame is then what wakes it,
    // or an LED frame the session painted would reach the device a poll late.
    Rig           rig;
    Facet         facet(rig);
    LaunchpadRpc& rpc = facet.start();
    const rp_provider* provider = rp_launchpad_provider();
    REQUIRE(rpc.launchpadClaim("LP in", "LP out", {}).connected);
    ControllerLink& link = rp::launchpad::providerLink(facet.self);
    link.pushOutput(Bytes{0xB0, 0x10, 0x04}.data(), 3);
    // Nothing has woken the writer, and its own wait is 50 ms: a frame's wake is what writes this. The link's own count
    // is polled, never the fake's record, which the writer thread is what writes.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(link.messagesSent() == 0);
    provider->wake(facet.self);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (link.messagesSent() == 0 && std::chrono::steady_clock::now() < deadline) provider->wake(facet.self);
    REQUIRE(link.messagesSent() == 1);
    // Released first: disconnecting joins the writer thread, so the fake's record is read with no thread writing it.
    rpc.launchpadRelease();
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xB0, 0x10, 0x04}});
}

// ---- a reconnect with audio running ---------------------------------------------------------------------------

TEST_CASE("ControllerLink: what an earlier claim received never reaches the next one", "[launchpad][link]") {
    // The audio thread is the input ring's ONLY consumer; a reconnect does not rewind it from the UI thread (that made
    // the UI thread a second consumer, and a mid-drain reconnect a double free). Instead each slot carries the claim it
    // arrived under, and a slot from an earlier claim is dropped on drain.
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("A in", "A out", {}));
    rig.deliver("A in", {0x90, 0x0B, 0x7F});  // still queued when the swap happens
    REQUIRE(link.connect("B in", "B out", {}));
    rig.deliver("B in", {0x90, 0x0C, 0x7F});
    CHECK(drained(link) == std::vector<Bytes>{{0x90, 0x0C, 0x7F}});  // the earlier claim's is gone
}

TEST_CASE("ControllerLink: a new claim's FIRST message is already its own", "[launchpad][link]") {
    // The claim's number is fixed before the input opens, so the receiver is bound to it: nothing can arrive tagged
    // with the previous claim or with "nothing".
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("A in", "A out", {}));
    drained(link);
    REQUIRE(link.connect("B in", "B out", {}));
    rig.deliver("B in", {0x90, 0x01, 0x01});
    CHECK(drained(link) == std::vector<Bytes>{{0x90, 0x01, 0x01}});
}

TEST_CASE("ControllerLink: output queued for an earlier claim is not written to the next device", "[launchpad][link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("A in", "A out", {}));
    push(link, {0xB0, 0x10, 0x7F});  // for A, never written: the claim ends first
    REQUIRE(link.connect("B in", "B out", {}));
    link.pump();
    CHECK(rig.written["B out"].empty());
    push(link, {0xB0, 0x10, 0x00});
    link.pump();
    CHECK(rig.written["B out"] == std::vector<Bytes>{{0xB0, 0x10, 0x00}});
}

TEST_CASE("a swap mid-drain leaves the audio thread a whole stream and no stale claim's message",
          "[launchpad][link][swap]") {
    // The race the fixed-slot ring exists for: the UI thread claims and releases while the audio thread drains and
    // pushes, and the device answers on its own (a port's callback) thread. Nothing may be rewritten under a drain,
    // nothing may be freed on the audio thread, every message the drain hands back must be a whole one from SOME claim,
    // and the claims must arrive in the order they were made - which is what "an earlier claim's slots are skipped in
    // place" means. Output follows the same rule: what lands on claim N's port carries claim N's bytes and no other's.
    // Run this file under ThreadSanitizer (tools/run-sanitizer.sh) and the rings are checked, not merely exercised.
    Rig               rig;
    ControllerLink    link(openerFor(rig), true);
    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};
    std::atomic<int>  outOfOrder{0};
    std::atomic<std::uint64_t> received{0};
    constexpr int     kClaims = 200;

    std::thread audio([&] {
        DeviceMessage slots[32];
        int           lastTag = -1;
        int           iter    = 0;
        while (!stop.load(std::memory_order_acquire)) {
            const std::size_t n = link.drainInput(slots, std::size(slots));
            for (std::size_t i = 0; i < n; ++i) {
                // Every scripted message is {claim, 0x55}: a torn slot would show any other second byte, or a size
                // that is not two.
                if (slots[i].size != 2 || slots[i].data[1] != 0x55) torn.store(true, std::memory_order_release);
                // An earlier claim's message never follows a later one's: stale slots are skipped where they sit.
                const int tag = slots[i].data[0];
                if (tag < lastTag) outOfOrder.fetch_add(1, std::memory_order_relaxed);
                lastTag = tag;
                received.fetch_add(1, std::memory_order_relaxed);
            }
            // Paint from the audio thread too (a frame in 64: the drain is the tight loop here): the link reads the
            // generation it pushes under, and the writer thread is what writes it out.
            if ((iter++ & 0x3f) == 0) push(link, {0xB0, static_cast<std::uint8_t>(link.generation() & 0xff), 0x55});
        }
    });

    for (int claim = 0; claim < kClaims; ++claim) {
        const auto        tag = static_cast<std::uint8_t>(claim);
        const std::string out = "out " + std::to_string(claim);
        REQUIRE(link.connect("LP in", out, kCloseBytes));
        // The device answers on the port's own callback thread, and only while this claim is the one connected.
        std::thread device([&] {
            for (int i = 0; i < 4; ++i) rig.deliver("LP in", {tag, 0x55});
        });
        device.join();
        link.disconnect();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    CHECK_FALSE(torn.load());
    CHECK(outOfOrder.load() == 0);
    // Nothing delivered under a claim that was already released was lost to the ring's bound here (4 messages into 256
    // slots), so every one of them was drained: the bound itself is held by the drop cases.
    CHECK(received.load() == static_cast<std::uint64_t>(kClaims) * 4);
    CHECK(link.generation() == 0);

    // Output: a message queued for claim N is on claim N's port or nowhere. Every port the link wrote to is one claim's.
    for (int claim = 0; claim < kClaims; ++claim) {
        const std::string out = "out " + std::to_string(claim);
        for (const Bytes& m : rig.written[out]) {
            if (m == kCloseBytes) continue;  // the goodbye is this claim's too, and goes out on the same port
            REQUIRE(m.size() == 3);
            INFO("claim " << claim << " (generation " << claim + 1 << ") wrote tag " << static_cast<int>(m[1]));
            // The audio thread reads the generation to build the message and the link reads it AGAIN to queue it, so a
            // paint made across a connect carries what the audio thread saw - nothing (0), the claim before this one, or
            // this one - and is still queued and written for this claim. What must never happen is an OLDER claim's bytes
            // on a later port: that is the stale slot this link exists to drop.
            CHECK((m[1] == 0 || m[1] == static_cast<std::uint8_t>(claim) ||
                   m[1] == static_cast<std::uint8_t>(claim + 1)));
        }
    }
}

// ---- MidiPortPool: the loose ports a scan opens ----------------------------------------------------------------

TEST_CASE("MidiPortPool: what an input hears is read once, in order", "[launchpad][pool]") {
    Rig          rig;
    MidiPortPool pool(openerFor(rig));
    const int    in = pool.openInputs({"Scan in"}).front();
    REQUIRE(in != 0);
    rig.deliver("Scan in", {0xF0, 0x01, 0xF7});
    rig.deliver("Scan in", {0xF0, 0x02, 0xF7});
    CHECK(pool.read(in) == std::vector<Bytes>{{0xF0, 0x01, 0xF7}, {0xF0, 0x02, 0xF7}});
    CHECK(pool.read(in).empty());
}

TEST_CASE("MidiPortPool: an output handle writes, an input handle does not", "[launchpad][pool]") {
    Rig          rig;
    MidiPortPool pool(openerFor(rig));
    const int    out = pool.openOutput("Scan out");
    const int    in  = pool.openInputs({"Scan in"}).front();
    REQUIRE(out != 0);
    REQUIRE(in != 0);
    CHECK(pool.send(out, Bytes{0xF0, 0x7E, 0xF7}.data(), 3));
    CHECK(rig.written["Scan out"] == std::vector<Bytes>{{0xF0, 0x7E, 0xF7}});
    CHECK_FALSE(pool.send(in, Bytes{0x90, 0x01, 0x01}.data(), 3));  // an input does not write
    CHECK_FALSE(pool.send(999, Bytes{0x90, 0x01, 0x01}.data(), 3));  // nor does an unknown handle
}

TEST_CASE("MidiPortPool: a port that will not open is a 0 handle and a reason", "[launchpad][pool]") {
    Rig          rig;
    rig.refuse.insert("Gone");
    MidiPortPool pool(openerFor(rig));
    CHECK(pool.openOutput("Gone") == 0);
    CHECK(pool.lastError() == "MIDI port not found: Gone");
    CHECK(pool.openInputs({"Gone", "Scan in"}).front() == 0);
    // The batch's FIRST refusal is the reason said, not whichever port happened to be last.
    CHECK(pool.lastError() == "MIDI port not found: Gone");
}

TEST_CASE("MidiPortPool: a full queue drops the newest and counts it", "[launchpad][pool]") {
    Rig          rig;
    MidiPortPool pool(openerFor(rig));
    const int    in = pool.openInputs({"Chatty in"}).front();
    REQUIRE(in != 0);
    for (std::size_t i = 0; i < MidiPortPool::kMaxQueued + 5; ++i) rig.deliver("Chatty in", {0x90, 0x01, 0x01});
    CHECK(pool.read(in).size() == MidiPortPool::kMaxQueued);
    CHECK(pool.dropped(in) == 5);
}

TEST_CASE("MidiPortPool: close stops delivery", "[launchpad][pool]") {
    Rig          rig;
    MidiPortPool pool(openerFor(rig));
    const int    in = pool.openInputs({"Scan in"}).front();
    REQUIRE(in != 0);
    pool.close(in);
    rig.deliver("Scan in", {0xF0, 0x01, 0xF7});  // the receiver is gone with the port
    CHECK(pool.read(in).empty());
    CHECK(rig.listening.empty());
}

TEST_CASE("MidiPortPool brackets held inputs, so the host can free its own stream", "[launchpad][pool]") {
    // A Windows MIDI input is exclusive: while the pool holds any input, the host takes its shared stream's hardware
    // inputs down, or a scan could not hear the very interface the user picked as their input.
    Rig          rig;
    // The callback's owner outlives the pool: the pool fires the edge in its destructor, which is how a scan that died
    // half way gives the inputs back.
    std::vector<bool> edges;
    MidiPortPool pool(openerFor(rig));
    pool.setOnInputsHeld([&](bool held) { edges.push_back(held); });
    const int a = pool.openInputs({"A in"}).front();
    REQUIRE(a != 0);
    CHECK(edges == std::vector<bool>{true});  // raised before the opens, so the stream let go first
    const int b = pool.openInputs({"B in"}).front();
    REQUIRE(b != 0);
    CHECK(edges == std::vector<bool>{true});  // already held: no second edge
    pool.close(a);
    CHECK(edges == std::vector<bool>{true});  // one input still held
    pool.close(b);
    CHECK(edges == std::vector<bool>{true, false});
    // Outputs deliberately do not count: the host's output is the user's synth, and cutting it for a scan would hang
    // held notes.
    const int out = pool.openOutput("Scan out");
    REQUIRE(out != 0);
    CHECK(edges == std::vector<bool>{true, false});
}

TEST_CASE("MidiPortPool opens a batch of inputs as ONE held edge, however many refuse", "[launchpad][pool]") {
    Rig          rig;
    rig.refuse.insert("Gone in");
    rig.refuse.insert("Gone too");
    std::vector<bool> edges;
    MidiPortPool pool(openerFor(rig));
    pool.setOnInputsHeld([&](bool held) { edges.push_back(held); });
    const std::vector<int> handles = pool.openInputs({"Gone in", "A in", "Gone too"});
    REQUIRE(handles.size() == 3);
    CHECK(handles[0] == 0);
    CHECK(handles[1] != 0);
    CHECK(handles[2] == 0);
    CHECK(edges == std::vector<bool>{true});  // one edge for the batch, not one per leading port
    CHECK(pool.inputCount() == 1);
}

TEST_CASE("MidiPortPool gives the inputs back if the only open fails", "[launchpad][pool]") {
    Rig          rig;
    rig.refuse.insert("Gone in");
    std::vector<bool> edges;
    MidiPortPool pool(openerFor(rig));
    pool.setOnInputsHeld([&](bool held) { edges.push_back(held); });
    CHECK(pool.openInputs({"Gone in"}).front() == 0);
    CHECK(edges == std::vector<bool>{true, false});  // raised for the attempt, handed straight back
}

TEST_CASE("MidiPortPool gives the inputs back on closeAll - a scan that died half way", "[launchpad][pool]") {
    Rig          rig;
    // The callback's owner outlives the pool: the pool fires the edge in its destructor, which is how a scan that died
    // half way gives the inputs back.
    std::vector<bool> edges;
    MidiPortPool pool(openerFor(rig));
    pool.setOnInputsHeld([&](bool held) { edges.push_back(held); });
    REQUIRE(pool.openInputs({"A in", "B in"}).front() != 0);
    pool.closeAll();
    CHECK(edges == std::vector<bool>{true, false});
    CHECK(pool.inputCount() == 0);
    CHECK(rig.listening.empty());
}

// ---- the provider and its facet -------------------------------------------------------------------------------

TEST_CASE("the provider installs the link as the pack's device, and takes it back on destroy", "[launchpad][provider]") {
    Rig   rig;
    FakeHost host;
    void* self = rp::launchpad::createProvider(&host.table, openerFor(rig), [] { return LaunchpadPortsDto{}; });
    REQUIRE(self != nullptr);
    // Installed under the name and interface the surface nodes ask for (rp.host.devices/2), so a plan binds it.
    CHECK(host.installs == 1);
    CHECK(host.name == rp::launchpad::kDeviceName);
    CHECK(host.interfaceId == rp::launchpad::kSurfaceLinkInterface);
    CHECK(host.link != nullptr);
    // A reopened source of the same pointer: it IS the link the nodes drain.
    CHECK(static_cast<rp::launchpad::SurfaceLink*>(host.link) == &rp::launchpad::providerLink(self));
    rp_launchpad_provider()->destroy(self);
    CHECK(host.clears == 1);
    CHECK(host.link == nullptr);
}

TEST_CASE("the provider refuses a host of another ABI version", "[launchpad][provider]") {
    Rig      rig;
    FakeHost host;
    host.table.abi_version = RP_PROVIDER_ABI_VERSION + 1;
    CHECK(rp::launchpad::createProvider(&host.table, openerFor(rig), [] { return LaunchpadPortsDto{}; }) == nullptr);
    CHECK(host.installs == 0);
}

TEST_CASE("the facet lists EVERY port, unfiltered, and names no device", "[launchpad][provider][facet]") {
    // A Launchpad also speaks TRS/DIN, so on a machine short of USB ports it arrives through an ordinary MIDI
    // interface on a port named after the INTERFACE: filtering here would make exactly that setup unconfigurable.
    Rig  rig;
    auto lister = [] {
        return LaunchpadPortsDto{{"Arturia KeyStep 32", "LPProMK3 MIDI", "MIDISPORT 2x2 Port A"},
                                 {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A"}};
    };
    FakeHost host;
    void* self = rp::launchpad::createProvider(&host.table, openerFor(rig), lister);
    REQUIRE(self != nullptr);
    auto& rpc = *static_cast<LaunchpadRpc*>(rp_launchpad_provider()->rpc_service(self));
    const LaunchpadPortsDto ports = rpc.launchpadPorts();
    CHECK(ports.inputs.size() == 3);
    CHECK(ports.outputs.size() == 2);
    CHECK(ports.inputs[1] == "LPProMK3 MIDI");
    CHECK(ports.outputs[0] == "LPProMK3 MIDI");  // an interface's input and output commonly carry one name
    rp_launchpad_provider()->destroy(self);
}

TEST_CASE("the facet keeps the pair out of the shared stream BEFORE it opens it, and gives it back on release",
          "[launchpad][provider][facet]") {
    // A Windows input the stream still held could not be opened at all, so the reservation must land first.
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc   = facet.start();
    int           opens = 0;
    for (const auto& e : rig.events) opens += e.rfind("open ", 0) == 0 ? 1 : 0;
    CHECK(opens == 0);
    const LaunchpadStatusDto claimed = rpc.launchpadClaim("LP in", "LP out", bytes(kCloseBytes));
    CHECK(claimed.connected);
    CHECK(claimed.generation == 1);
    // The claim first hands the shared stream back whatever it had (nothing here), then reserves the new pair, and only
    // then opens it.
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "LP in|LP out"});
    CHECK(rig.events.back() == "open in:LP in");  // opened after both, the last thing that happened
    CHECK(rig.listening.count("LP in") == 1);

    const LaunchpadStatusDto released = rpc.launchpadRelease();
    CHECK_FALSE(released.connected);
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "LP in|LP out", "|"});
    CHECK(rig.written["LP out"] == std::vector<Bytes>{kCloseBytes});
}

TEST_CASE("the facet claims a pair whose input and output have ONE name, as an interface's ports do",
          "[launchpad][provider][facet]") {
    // ALSA's "client:port N:M", CoreMIDI's endpoint name and WinMM's "LPProMK3 MIDI" are the same in both directions.
    // Both claims are one lock then: a second claim of the same name is refused by this process's own flock /
    // unshared CreateFile, so claiming twice would make the ordinary device unclaimable. The config dir is REAL, since
    // that is where the lock lives.
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    const LaunchpadStatusDto claimed = rpc.launchpadClaim("LPProMK3 MIDI", "LPProMK3 MIDI", bytes(kCloseBytes));
    INFO(claimed.error);
    CHECK(claimed.connected);
    CHECK(claimed.input == "LPProMK3 MIDI");
    CHECK(claimed.output == "LPProMK3 MIDI");
    CHECK(claimed.generation == 1);
    CHECK(rig.listening.count("LPProMK3 MIDI") == 1);
    CHECK(rpc.launchpadRelease().connected == false);
    CHECK(rig.written["LPProMK3 MIDI"] == std::vector<Bytes>{kCloseBytes});  // the one output it holds
}

TEST_CASE("the facet keeps ONE claim per port across processes, through the product's config dir",
          "[launchpad][provider][facet]") {
    // The production path: the provider installs claimPort under <configDir>/locks, so a port another process holds is
    // refused before anything opens, and the refusal keeps the pair out of this process's shared stream.
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    // A second opener of the same port, from this process here, is refused exactly as another process's would be.
    const auto held = rp::launchpad::claimPort(facet.dir.str(), "LPProMK3 MIDI");
    REQUIRE(held != nullptr);
    const LaunchpadStatusDto refused = rpc.launchpadClaim("LPProMK3 MIDI", "LPProMK3 MIDI", {});
    CHECK_FALSE(refused.connected);
    CHECK(refused.error == "in use by another RetroPlug");
    CHECK(rig.events.empty());  // nothing opened
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "LPProMK3 MIDI|LPProMK3 MIDI"});
    // Given back, it claims, and the lock the refused claim would have held is this claim's alone.
    CHECK_FALSE(rpc.launchpadRelease().connected);
}

TEST_CASE("the facet gives the reservation back when the claim fails", "[launchpad][provider][facet]") {
    Rig   rig;
    rig.refuse.insert("Gone in");
    Facet facet(rig);
    LaunchpadRpc& rpc     = facet.start();
    const LaunchpadStatusDto refused = rpc.launchpadClaim("Gone in", "LP out", {});
    CHECK_FALSE(refused.connected);
    CHECK(refused.error == "MIDI port not found: Gone in");
    // Reserved for the attempt, then handed straight back: a failed claim must not keep the pair out of the shared
    // stream with nothing holding it.
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "Gone in|LP out", "|"});
    CHECK(rig.listening.empty());
}

TEST_CASE("the facet lets the OLD pair go before it reserves the new one", "[launchpad][provider][facet]") {
    // Swapping pairs must not leave the second one waiting on the first: by the time B is reserved (and A therefore
    // rejoins the shared stream) A has been closed, with its close bytes, and its claims are given back.
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    REQUIRE(rpc.launchpadClaim("A in", "A out", bytes(kCloseBytes)).connected);
    const LaunchpadStatusDto swapped = rpc.launchpadClaim("B in", "B out", bytes(kOtherClose));
    CHECK(swapped.connected);
    CHECK(swapped.generation == 2);
    CHECK(rig.listening.count("A in") == 0);
    CHECK(rig.written["A out"] == std::vector<Bytes>{kCloseBytes});
    // The reservation the product saw, in order: A's given back, then B's.
    CHECK(facet.host.reserved ==
          std::vector<std::string>{"|", "A in|A out", "|", "B in|B out"});
    CHECK(rig.events.back() == "open in:B in");
}

TEST_CASE("the facet refuses a pick whose ports are not here, and reserves nothing for it",
          "[launchpad][provider][facet]") {
    // Reserving for a pick that is not enumerated would reopen the shared stream twice (reserve, then hand back) - two
    // audio restarts - for a claim that cannot succeed.
    Rig  rig;
    auto lister = [] { return LaunchpadPortsDto{{"LP in"}, {"LP out"}}; };
    FakeHost host;
    void* self = rp::launchpad::createProvider(&host.table, openerFor(rig), lister);
    REQUIRE(self != nullptr);
    auto& rpc = *static_cast<LaunchpadRpc*>(rp_launchpad_provider()->rpc_service(self));

    const LaunchpadStatusDto noInput = rpc.launchpadClaim("Unplugged in", "LP out", {});
    CHECK_FALSE(noInput.connected);
    CHECK(noInput.error == "not connected: Unplugged in");
    const LaunchpadStatusDto noOutput = rpc.launchpadClaim("LP in", "Unplugged out", {});
    CHECK(noOutput.error == "not connected: Unplugged out");
    // Each claim hands the old pair back (nothing was held) and its own refusal hands nothing back again: empty names.
    CHECK(host.reserved == std::vector<std::string>{"|", "|", "|", "|"});
    CHECK(rig.events.empty());                                   // nothing reserved, nothing opened

    REQUIRE(rpc.launchpadClaim("LP in", "LP out", {}).connected);
    CHECK(host.reserved.back() == "LP in|LP out");
    rp_launchpad_provider()->destroy(self);
}

TEST_CASE("the facet refuses to guess a port, and says why", "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    const LaunchpadStatusDto none = rpc.launchpadClaim("", "LP out", {});
    CHECK_FALSE(none.connected);
    CHECK(none.error == "no port chosen");
    CHECK(rig.events.empty());  // nothing was opened on a guess
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "|"});  // never a pair
}

TEST_CASE("the facet refuses a port another process has claimed, and opens nothing", "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    std::vector<std::string> claims;
    const std::set<std::string> heldElsewhere{"Taken in"};
    rpc.setClaim([&](const std::string& port) -> PortClaim {
        claims.push_back(port);
        if (heldElsewhere.count(port)) return nullptr;
        return std::make_shared<int>(0);
    });
    const LaunchpadStatusDto refused = rpc.launchpadClaim("Taken in", "Taken out", {});
    CHECK_FALSE(refused.connected);
    CHECK(refused.error == "in use by another RetroPlug");
    CHECK(rig.events.empty());  // no port opened
    // ...but the pair IS kept out of this process's shared stream: otherwise it would hear every pad the other
    // RetroPlug takes, as raw notes - a row launch in this cart too - and mirror its own MIDI onto that grid.
    CHECK(facet.host.reserved == std::vector<std::string>{"|", "Taken in|Taken out"});

    // Both ports are claimed: two instances with different inputs must not share one output either.
    const LaunchpadStatusDto claimed = rpc.launchpadClaim("LP in", "LP out", {});
    CHECK(claimed.connected);
    CHECK(claims == std::vector<std::string>{"Taken in", "Taken out", "LP in", "LP out"});
}

TEST_CASE("the facet's restored reaches the product, and the scan's held edge is the product's",
          "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    CHECK(rpc.launchpadRestored());          // the facet's own half is always ready
    CHECK(facet.host.restoredCalls == 1);    // and the product heard it, through the host table (no pack class named);
    const std::int32_t in = rpc.launchpadOpenInputs({"A in"}).front();
    REQUIRE(in != 0);
    CHECK(facet.host.suspended == std::vector<bool>{true});
    rpc.launchpadPortClose(in);
    CHECK(facet.host.suspended == std::vector<bool>{true, false});
}

TEST_CASE("the facet's pool half writes, reads and closes by handle", "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    const std::int32_t out = rpc.launchpadOpenOutput("Scan out");
    const std::int32_t in  = rpc.launchpadOpenInputs({"Scan in"}).front();
    REQUIRE(out != 0);
    REQUIRE(in != 0);
    CHECK(rpc.launchpadPortSend(out, bytes({0xF0, 0x7E, 0xF7})));
    CHECK(rig.written["Scan out"] == std::vector<Bytes>{{0xF0, 0x7E, 0xF7}});
    rig.deliver("Scan in", {0xF0, 0x06, 0xF7});
    const auto read = rpc.launchpadPortRead(in);
    REQUIRE(read.messages.size() == 1);
    const auto* readAt = reinterpret_cast<const std::uint8_t*>(read.messages[0].data());
    CHECK(Bytes(readAt, readAt + read.messages[0].size()) == Bytes{0xF0, 0x06, 0xF7});
    CHECK(read.dropped == 0);
    CHECK(rpc.launchpadPortClose(in));
    CHECK(rig.listening.empty());
}

TEST_CASE("the facet mounts every one of its methods on the product's server", "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    CountingServer server;
    rp::launchpad::registerRpc(server, &rpc);
    // ports, status, claim, release, send, restored, and the pool's six.
    CHECK(server.methods == 12);
    // A provider that did not start mounts nothing, so the UI reads the host as having no control-surface seam.
    CountingServer none;
    rp::launchpad::registerRpc(none, nullptr);
    CHECK(none.methods == 0);
}

TEST_CASE("the facet says what the pair is doing, and sends a message that is not the session's",
          "[launchpad][provider][facet]") {
    Rig   rig;
    Facet facet(rig);
    LaunchpadRpc& rpc = facet.start();
    rpc.launchpadClaim("LP in", "LP out", bytes(kCloseBytes));
    const LaunchpadStatusDto status = rpc.launchpadStatus();
    CHECK(status.connected);
    CHECK(status.generation == 1);
    CHECK(status.input == "LP in");
    CHECK(status.output == "LP out");
    CHECK(status.error.empty());
    CHECK(status.sent == 0);
    // Letting the device go while the pair stays claimed: the bytes go out, the claim stays.
    CHECK(rpc.launchpadSend(bytes({0xF0, 0x00, 0xF7})));
    CHECK(rig.written["LP out"] == std::vector<Bytes>{{0xF0, 0x00, 0xF7}});
    CHECK(rpc.launchpadStatus().connected);
}

TEST_CASE("claimPort: one holder per port across the whole machine, released with the token", "[launchpad][claim]") {
    const auto dir = (std::filesystem::temp_directory_path() / "rp-portclaim-launchpad-test").string();
    std::filesystem::remove_all(dir);
    auto first = rp::launchpad::claimPort(dir, "LPProMK3 MIDI:LPProMK3 MIDI 128:0");
    REQUIRE(first != nullptr);
    // A second claim - from this process here, but flock / an unshared CreateFile refuse any second opener.
    CHECK(rp::launchpad::claimPort(dir, "LPProMK3 MIDI:LPProMK3 MIDI 128:0") == nullptr);
    CHECK(rp::launchpad::claimPort(dir, "Some other port") != nullptr);  // a different device is unaffected
    first.reset();
    CHECK(rp::launchpad::claimPort(dir, "LPProMK3 MIDI:LPProMK3 MIDI 128:0") != nullptr);
    std::filesystem::remove_all(dir);
}
