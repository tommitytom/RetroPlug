// The Pico box as a graph host (docs/modular-graph-phase-e3-design.md sections 5 and 8), its portable pieces built for
// the host: the generic box host (pico/host: the block driver, the staging, the device rp_host and the built-in plan)
// and the N8 box's cart link (pico/n8-host/BoxCartLink.cpp), over the box's own plan (pico/n8-host/plan.json) exported
// as its firmware's build exports it and loaded over the device's builds of its types.
//
// - The block driver answers the time since the last block at the plan's rate, carrying what passes maxFrames up to
//   kCarryBlocks blocks, so the frames run add up to the time passed within that: a 12 s wait runs 4 blocks and counts
//   the rest `skipped`.
// - The staging stages whole messages, each with its status byte and as it came (nothing filtered), a SysEx whole and
//   the box's own F0 7D 50 commands taken by its hook; it takes no more than a block holds, and loses nothing to that:
//   a byte it refuses waits for the next pass.
// - What is staged reaches rp.device.midi-in at the frame its arrival falls on within the block, and the plan's MIDI
//   policy (rp.bliptoaster.midi-policy, a script node on the device's MicroQuickJS host) keeps what the bridge always
//   forwarded.
// - The cart link: its ring, its refusals (no game running, full), its batching into writes of up to 192 bytes, the
//   note-offs it is handed, the expansion volume at every connect and on a change, and the books that tell the
//   node's own note-offs from what the plan was handed.
// - The whole pass, bytes in to FIFO writes out, over the device's builds and the device's build of the link, with
//   nothing allocated; and the box's stats, counted at the plan's two edges.
// - plan.json's blob is its golden's (golden/box-plan.hex; RP_PLAN_BLOB_UPDATE=1 records it), as the tool writes it
//   for the firmware, and fits the box's arena.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "BlockDriver.hpp"
#include "BoxCartLink.hpp"
#include "FakeN8.hpp"
#include "LinkRomModel.hpp"
#include "BoxStats.hpp"
#include "BuiltInPlan.hpp"
#include "CartVolume.hpp"
#include "DeviceHost.hpp"
#include "DeviceModeEntries.hpp"
#include "MidiStaging.hpp"
#include "PlanNodes.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanExport.hpp"
#include "graph/PlanLoader.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "n8/CartLink.hpp"
#include "n8/Edio.hpp"
#include "n8/N8Node.hpp"
#include "script/MqjsHost.hpp"
#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"

// The bliptoaster script pack's registration, the desktop's and the device's (retroplug_device_mode_objects), and its
// source (its heap).
extern "C" const rp_package* rp_bliptoaster_script_package(void);
extern "C" const rp_package* rp_bliptoaster_script_package_device_mode(void);
extern "C" const rp_mqjs_pack_source rp_script_bliptoaster;
// The scripttest fixture pack's (packages/native/test/device-mode/script-packs/scripttest), for a script node's fault.
extern "C" const rp_package* rp_scripttest_script_package(void);
extern "C" const rp_package* rp_scripttest_script_package_device_mode(void);

namespace {

using namespace rp::graph;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kRate = RP_BOX_SAMPLE_RATE;
constexpr std::uint32_t kMaxFrames = RP_BOX_MAX_FRAMES;
constexpr std::size_t   kArenaBytes = RP_BOX_ARENA_BYTES;
// The box's arena as this host's builds need it for the box's plan: the box's figure, grown by what the bliptoaster
// pack's memory takes here over what it takes on the box (a 64-bit device build gives its context 3/2 of the heap,
// script/MqjsHost.hpp's kDeviceHalves, and its copy of the runtime's bytecode is the 64-bit one). Every other part of
// the plan is no smaller here than on the box, so a plan the export holds to the box's arena loads in this.
const std::size_t kHostArenaBytes =
    kArenaBytes + rp::mqjs::packageBytes(rp_script_bliptoaster) - rp::mqjs::boxPackageBytes(rp_script_bliptoaster);

// ---- the block driver's clock --------------------------------------------------------------------------------------

struct FakeClock {
    std::uint64_t us = 0;
    static std::uint64_t read(void* ctx) { return static_cast<FakeClock*>(ctx)->us; }
};

// ---- the box's plan ------------------------------------------------------------------------------------------------

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string hexOf(const Bytes& bytes) {
    std::string out;
    char        digits[3];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(digits, sizeof digits, "%02x", bytes[i]);
        out += digits;
        out += (i % 32 == 31 || i + 1 == bytes.size()) ? '\n' : ' ';
    }
    return out;
}

// What the box registers (its product: core, device-io, n8 and the bliptoaster script pack), as the device builds them.
const std::vector<const rp_package*>& boxTypes() {
    static const std::vector<const rp_package*> types{rp_core_package_device_mode(), rp_device_io_package_device_mode(),
                                                      rp_n8_package_device_mode(),
                                                      rp_bliptoaster_script_package_device_mode()};
    return types;
}

// plan.json exported as the firmware's build exports it (pico/n8-host/plan.cmake's rate, block and arena).
ExportResult boxExport() {
    NodeRegistry registry;
    for (const rp_package* p :
         {rp_core_package(), rp_device_io_package(), rp_n8_package(), rp_bliptoaster_script_package()}) {
        std::string why;
        REQUIRE(registry.addPackage(p, &why));
    }
    ExportOptions o;
    o.sampleRate = kRate;
    o.maxFrames = kMaxFrames;
    o.arenaBytes = kArenaBytes;
    return exportPlan(readFile(RP_BOX_PLAN), registry, boxTypes(), o);
}

const Bytes& boxBlob() {
    static const Bytes blob = [] {
        const ExportResult r = boxExport();
        std::string        errors;
        for (const auto& e : r.errors) errors += e.code + " " + e.nodeId + ": " + e.message + "\n";
        INFO(errors);
        REQUIRE(r.ok());
        return r.blob;
    }();
    return blob;
}

struct Arena {
    std::vector<std::max_align_t> storage;
    explicit Arena(std::size_t bytes) : storage(bytes / sizeof(std::max_align_t) + 1) {}
    std::span<std::byte> span(std::size_t bytes) { return {reinterpret_cast<std::byte*>(storage.data()), bytes}; }
};

// Both hold their own address in the interface structs they hand out, so neither is copied.
static_assert(!std::is_copy_constructible_v<rp::box::MidiStaging> && !std::is_copy_assignable_v<rp::box::MidiStaging>);
static_assert(!std::is_copy_constructible_v<rp::box::DeviceHost> && !std::is_copy_assignable_v<rp::box::DeviceHost>);

std::vector<std::string> gLines;
void                     collect(const char* line) { gLines.emplace_back(line); }
void                     ignoreLog(int, const char*) {}

// ---- the staging, by itself ----------------------------------------------------------------------------------------

struct Staged {
    std::uint32_t frame;
    Bytes         bytes;
    bool operator==(const Staged&) const = default;
};

// Everything the staging hands one block of `frames`, as rp.device.midi-in takes it, and the block ended as the host
// ends it.
std::vector<Staged> take(rp::box::MidiStaging& s, std::uint32_t frames = kMaxFrames) {
    std::vector<Staged>    out;
    rp_device_midi_message m{};
    const rp_device_midi_in* in = s.interface();
    while (in->next(in->ctx, frames, &m)) out.push_back({m.frame, Bytes(m.data, m.data + m.size)});
    s.endBlock();
    return out;
}

// Bytes fed in order, each arriving 320 us after the last (one byte on the 31250 baud wire); false when one is refused.
bool feed(rp::box::MidiStaging& s, const Bytes& bytes, std::uint32_t& at) {
    for (std::uint8_t b : bytes) {
        if (!s.feed(b, at)) return false;
        at += 320;
    }
    return true;
}

std::uint32_t noFrame(void*, std::uint32_t) { return 0; }

// The box's hook: its F0 7D 50 commands taken, the last kept (in place, so a pass allocates nothing).
struct SysexTaken {
    std::uint32_t count = 0;
    std::uint8_t  last[16] = {};
    std::uint32_t lastSize = 0;
    static bool   hook(void* ctx, const std::uint8_t* d, std::uint32_t n) {
        if (n < 4 || d[1] != 0x7D || d[2] != 0x50) return false;
        auto& self = *static_cast<SysexTaken*>(ctx);
        ++self.count;
        self.lastSize = n < sizeof self.last ? n : static_cast<std::uint32_t>(sizeof self.last);
        for (std::uint32_t i = 0; i < self.lastSize; ++i) self.last[i] = d[i];
        return true;
    }
    Bytes lastTaken() const { return Bytes(last, last + lastSize); }
};

// ---- the box's pass ------------------------------------------------------------------------------------------------

// The box as main.cpp builds it, over a fake clock: the staging, the driver and the host, the built-in plan loaded in
// an arena of the box's size, and a cart link (the device's build of the box's link, or the device's fake one).
struct Box {
    FakeClock                       clock{1'000'000};
    rp::box::ElapsedDriver          driver;
    std::unique_ptr<rp::box::MidiStaging> staging = std::make_unique<rp::box::MidiStaging>();
    rp::box::DeviceHost             host;
    rp::box::BuiltInPlan            plan;
    rp::box::DeviceLink             links[1]{};
    rp_host_clock                   scriptClock{&clock, &FakeClock::read, 0};  // main.cpp's, on the fake clock
    Arena                           arena{kHostArenaBytes};
    SysexTaken                      sysex;
    // The UART's ring: each byte and its arrival, read from `ringAt` (a vector, so a pass frees nothing).
    std::vector<std::pair<std::uint8_t, std::uint32_t>> ring;
    std::size_t                     ringAt = 0;
    std::size_t                     ringEnd = SIZE_MAX;  // what a pass may read up to: a byte past it has not come

    static std::uint32_t frameOf(void* ctx, std::uint32_t us) { return static_cast<Box*>(ctx)->driver.frameOf(us); }

    // `link` is installed as the cart ("n8", rp.n8.cart-link/1), with `acquired` and `released` as its hooks.
    void load(void* link, void (*acquired)(void*) = nullptr, void (*released)(void*) = nullptr) {
        loadBlob(boxBlob(), link, acquired, released);
    }
    // Another blob (`blob` empty: no plan at all, as a refused one leaves the box).
    void loadBlob(const Bytes& blob, void* link, void (*acquired)(void*) = nullptr, void (*released)(void*) = nullptr) {
        staging->init(&SysexTaken::hook, &sysex, &frameOf, this);
        links[0] =
            rp::box::DeviceLink{rp::n8::kDeviceName, rp::n8::kCartLinkInterface, link, nullptr, acquired, released};
        host.init(links, 1, staging->interface(), &ignoreLog, &scriptClock);
        gLines.clear();
        if (!blob.empty()) REQUIRE(plan.load(blob, arena.span(kHostArenaBytes), boxTypes(), host.host(), &collect));
        driver.start(&FakeClock::read, &clock, kRate, kMaxFrames);
    }
    // Bytes onto the UART's ring, each 320 us after the one before, the clock moving with them.
    void arrive(const Bytes& bytes) {
        for (std::uint8_t b : bytes) {
            clock.us += 320;
            ring.emplace_back(b, static_cast<std::uint32_t>(clock.us));
        }
    }
    // One pass's parse and block, `us` after the last moment, as main.cpp runs it: the ring into the staging until it
    // refuses, then the block the time makes, which the host ends; with no plan, what is staged is dropped. True when
    // a block ran.
    bool pass(std::uint64_t us = 0) {
        clock.us += us;
        const std::size_t end = ring.size() < ringEnd ? ring.size() : ringEnd;
        while (ringAt < end && staging->feed(ring[ringAt].first, ring[ringAt].second)) ++ringAt;
        if (!plan.plan()) {
            staging->clearStaged();
            return false;
        }
        if (!plan.run(driver)) return false;
        staging->endBlock();
        return true;
    }
};

std::string lines(const std::vector<Staged>& v) {
    std::ostringstream o;
    for (const Staged& s : v) {
        o << s.frame << ":";
        for (std::uint8_t b : s.bytes) o << " " << std::hex << int(b) << std::dec;
        o << "\n";
    }
    return o.str();
}

std::string pushedLines(const CartRecord& r) {
    std::vector<Staged> v;
    for (std::uint32_t i = 0; i < r.count && i < CartRecord::kPushes; ++i)
        v.push_back({r.pushes[i].frame, Bytes(r.pushes[i].bytes, r.pushes[i].bytes + r.pushes[i].size)});
    return lines(v);
}

}  // namespace

TEST_CASE("the box's block driver answers the time since the last block, carrying what passes maxFrames",
          "[device-mode][box]") {
    FakeClock              clock{5'000'000};
    rp::box::ElapsedDriver driver;
    driver.start(&FakeClock::read, &clock, kRate, kMaxFrames);
    std::uint32_t frames = 0;
    rp_transport  transport{};

    // Nothing due until a whole frame has passed (20.8 us at 48 kHz).
    CHECK_FALSE(driver.next(&frames, &transport));
    clock.us += 20;
    CHECK_FALSE(driver.next(&frames, &transport));
    clock.us += 1;
    REQUIRE(driver.next(&frames, &transport));
    CHECK(frames == 1);
    CHECK(transport.sample_rate == 48000.0);
    CHECK(transport.flags == 0u);

    // An ordinary stall (a console line, about 11 ms): 528 frames, carried whole, as 256, 256 and 16.
    clock.us += 11'000;
    std::uint64_t blocks = 0, total = 1;
    while (driver.next(&frames, &transport)) {
        ++blocks;
        total += frames;
    }
    CHECK(blocks == 3);
    CHECK(total == (11'021 * 48) / 1000);
    CHECK(driver.skipped() == 0);

    // A 12 s wait (an install through the N8's menu): 576,000 frames due, of which the last kCarryBlocks blocks run
    // (4 of 256) and the rest is dropped and counted, never run.
    const std::uint64_t before = total;
    clock.us += 12'000'000;
    blocks = 0;
    while (driver.next(&frames, &transport)) {
        CHECK(frames <= kMaxFrames);
        ++blocks;
        total += frames;
    }
    CHECK(blocks == rp::box::ElapsedDriver::kCarryBlocks);
    CHECK(total - before == rp::box::ElapsedDriver::kCarryBlocks * kMaxFrames);
    CHECK(total + driver.skipped() == ((11'021 + 12'000'000) * 48) / 1000);  // every frame run or counted
    CHECK(driver.carried() == 0);

    // Frames are counted from the start, so a fraction of one is carried, never lost: 7 us at a time for 7 ms runs
    // exactly 7 ms of frames.
    rp::box::ElapsedDriver fine;
    FakeClock              c2{0};
    fine.start(&FakeClock::read, &c2, kRate, kMaxFrames);
    std::uint64_t run = 0;
    for (int i = 0; i < 1000; ++i) {
        c2.us += 7;
        if (fine.next(&frames, &transport)) run += frames;
    }
    CHECK(run == 7000 * 48 / 1000);
}

TEST_CASE("the box's block driver places a moment on its block's frames, clamped into the block",
          "[device-mode][box]") {
    FakeClock              clock{0};
    rp::box::ElapsedDriver driver;
    driver.start(&FakeClock::read, &clock, kRate, kMaxFrames);
    std::uint32_t frames = 0;
    rp_transport  transport{};

    clock.us = 10'000;  // 480 frames due: a full block, and 224 carried
    REQUIRE(driver.next(&frames, &transport));
    CHECK(frames == 256);
    CHECK(driver.carried() == 224);
    CHECK(driver.frameOf(0) == 0);
    CHECK(driver.frameOf(2'000) == 96);
    CHECK(driver.frameOf(9'000) == 255);  // past the block (it runs behind the clock): its last frame
    REQUIRE(driver.next(&frames, &transport));
    CHECK(frames == 224);
    CHECK(driver.frameOf(9'000) == 432 - 256);
    CHECK(driver.frameOf(100) == 0);  // before the block: its first frame

    // Across the 32-bit microsecond wrap a timestamp holds.
    FakeClock              late{(std::uint64_t{1} << 32) - 1'000};
    rp::box::ElapsedDriver wrap;
    wrap.start(&FakeClock::read, &late, kRate, kMaxFrames);
    late.us += 3'000;
    REQUIRE(wrap.next(&frames, &transport));
    CHECK(frames == 144);
    CHECK(wrap.frameOf(static_cast<std::uint32_t>((std::uint64_t{1} << 32) - 500)) == 24);
    CHECK(wrap.frameOf(1'500) == 120);  // 2,500 us after the start, past the wrap
}

TEST_CASE("the box's staging stages whole messages as they came, a SysEx whole, and its own commands nowhere",
          "[device-mode][box]") {
    auto       s = std::make_unique<rp::box::MidiStaging>();
    SysexTaken box;
    s->init(&SysexTaken::hook, &box, &noFrame, nullptr);
    std::uint32_t at = 0;
    REQUIRE(feed(*s,
                 {
                     0x90, 0x3C, 0x64, 0x3E, 0x64,              // a note-on, and one under running status
                     0xB0, 0xF8, 0x07, 0x64,                    // a clock inside a control change
                     0xC3, 0x05, 0xD0, 0x20, 0xA1, 0x40, 0x10,  // program change, channel and poly aftertouch
                     0xF0, 0x7E, 0x7F, 0xFE, 0x06, 0x01, 0xF7,  // a SysEx with active sensing inside it
                     0xF0, 0x7D, 0x50, 0x01, 0x01, 0xF7,        // the box's own command
                     0xF2, 0x10, 0x20, 0xF1, 0x05, 0xF6,        // system common: song position, time code, tune
                     0x40, 0x41,                                // stray data with no status: nothing
                     0xF0, 0x01, 0x02, 0x80, 0x40, 0x00,        // a SysEx cut short by a note-off
                 },
                 at));
    const std::vector<Staged> want = {
        {0, {0x90, 0x3C, 0x64}}, {0, {0x90, 0x3E, 0x64}}, {0, {0xF8}}, {0, {0xB0, 0x07, 0x64}}, {0, {0xC3, 0x05}},
        {0, {0xD0, 0x20}},       {0, {0xA1, 0x40, 0x10}}, {0, {0xFE}}, {0, {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}},
        {0, {0xF2, 0x10, 0x20}}, {0, {0xF1, 0x05}},       {0, {0xF6}}, {0, {0xF0, 0x01, 0x02, 0xF7}},
        {0, {0x80, 0x40, 0x00}},
    };
    // The F6 is a status byte, so it cancels running status: the two stray bytes after it are no message.
    CHECK(lines(take(*s)) == lines(want));
    REQUIRE(box.count == 1);
    CHECK(box.lastTaken() == Bytes{0xF0, 0x7D, 0x50, 0x01, 0x01, 0xF7});
    CHECK(s->counts().channelMessages == 7);  // the note-ons, the control change, program change, both aftertouches
                                              // and the note-off
    CHECK(s->counts().handed == want.size());
    CHECK(s->counts().consumed == 1);
    CHECK(take(*s).empty());  // and the staging is empty for the next block

    // A message straddling the reset starts over: the stale half is gone, as the box drops stale MIDI.
    REQUIRE(feed(*s, {0x90, 0x3C}, at));
    s->reset();
    REQUIRE(feed(*s, {0x64, 0x91, 0x3C, 0x64}, at));
    CHECK(lines(take(*s)) == lines({{0, {0x91, 0x3C, 0x64}}}));
}

TEST_CASE("the box's staging takes no more than a block holds, and loses nothing to it", "[device-mode][box]") {
    auto s = std::make_unique<rp::box::MidiStaging>();
    s->init(nullptr, nullptr, &noFrame, nullptr);
    std::uint32_t at = 0;

    SECTION("300 note-ons: 64 a block, the rest a pass at a time") {
        constexpr std::uint32_t kBlock = rp::box::MidiStaging::kMessages;
        Bytes wire;
        for (int i = 0; i < 300; ++i) wire.insert(wire.end(), {0x90, static_cast<std::uint8_t>(i % 128), 0x40});
        std::size_t fed = 0;
        while (fed < wire.size() && s->feed(wire[fed], at)) ++fed;
        CHECK(fed == kBlock * 3);  // the 65th note-on's first byte is refused: the staging has no message left
        std::uint32_t taken = 0, blocks = 0;
        std::vector<Staged> last;
        while (taken < 300) {
            last = take(*s);
            REQUIRE(last.size() == std::min<std::uint32_t>(kBlock, 300 - taken));
            CHECK(last.front().bytes == Bytes{0x90, static_cast<std::uint8_t>(taken % 128), 0x40});
            taken += static_cast<std::uint32_t>(last.size());
            ++blocks;
            while (fed < wire.size() && s->feed(wire[fed], at)) ++fed;
        }
        CHECK(fed == wire.size());
        CHECK(blocks == 5);  // 64 four times, then 44
        CHECK(last.back().bytes == Bytes{0x90, 299 % 128, 0x40});
    }
    SECTION("a SysEx of exactly 4 KB fits; one byte more is dropped whole and counted") {
        Bytes sysex(rp::box::MidiStaging::kSysexBytes, 0x11);
        sysex.front() = 0xF0;
        sysex.back() = 0xF7;
        REQUIRE(feed(*s, sysex, at));
        auto got = take(*s);
        REQUIRE(got.size() == 1);
        CHECK(got[0].bytes == sysex);
        sysex.insert(sysex.begin() + 1, 0x22);
        REQUIRE(feed(*s, sysex, at));
        REQUIRE(feed(*s, {0x90, 0x3C, 0x64}, at));
        got = take(*s);
        REQUIRE(got.size() == 1);
        CHECK(got[0].bytes == Bytes{0x90, 0x3C, 0x64});
        CHECK(s->counts().tooLong == 1);
    }
    // 60 note-ons leave room for two messages more but 3916 bytes, short of a 4001-byte SysEx.
    SECTION("a SysEx with no room waits whole, and what the same byte completed waits behind it") {
        for (int i = 0; i < 60; ++i) REQUIRE(feed(*s, {0x90, static_cast<std::uint8_t>(i), 0x40}, at));
        Bytes sysex(4000, 0x33);
        sysex.front() = 0xF0;
        // Cut short by a tune request: the parser's F7, then the F6, completed by one byte.
        REQUIRE(feed(*s, sysex, at));
        REQUIRE(feed(*s, {0xF6}, at));
        CHECK_FALSE(s->feed(0x90, at));  // nothing more until a block has made room
        auto got = take(*s);
        CHECK(got.size() == 60);
        CHECK(s->staged() == 2);  // the block that made room staged the two for the next, with no byte to wait for
        REQUIRE(feed(*s, {0x90, 0x01, 0x02}, at));
        got = take(*s);
        REQUIRE(got.size() == 3);
        sysex.push_back(0xF7);
        CHECK(got[0].bytes == sysex);
        CHECK(got[1].bytes == Bytes{0xF6});
        CHECK(got[2].bytes == Bytes{0x90, 0x01, 0x02});
    }
    SECTION("a host with no plan drops what is staged and what is held, each counted") {
        for (int i = 0; i < 60; ++i) REQUIRE(feed(*s, {0x90, static_cast<std::uint8_t>(i), 0x40}, at));
        Bytes sysex(4000, 0x33);
        sysex.front() = 0xF0;
        REQUIRE(feed(*s, sysex, at));
        REQUIRE(feed(*s, {0xF6}, at));  // cuts the SysEx short: both wait for room, as above
        REQUIRE(s->counts().untaken == 0);
        REQUIRE(s->staged() == 60);  // the SysEx and the tune request are held, not staged
        s->clearStaged();
        CHECK(s->counts().untaken == 60 + 1 + 1);  // the staged messages, the held SysEx and the held tune request
        CHECK(s->staged() == 0);
        REQUIRE(feed(*s, {0x90, 0x01, 0x02}, at));  // and the staging takes bytes again
        const auto got = take(*s);
        REQUIRE(got.size() == 1);
        CHECK(got[0].bytes == Bytes{0x90, 0x01, 0x02});
    }
}

// A link and its hooks, to see what a DeviceHost tells them.
struct LinkBooks {
    int acquired = 0;
    int released = 0;
    static void onAcquired(void* ctx) { ++static_cast<LinkBooks*>(ctx)->acquired; }
    static void onReleased(void* ctx) { ++static_cast<LinkBooks*>(ctx)->released; }
};

TEST_CASE("the box's device host answers the link it installed by name and interface, and nothing else",
          "[device-mode][box]") {
    LinkBooks books;
    int       link = 0, stray = 0;
    const rp::box::DeviceLink links[] = {{"n8", "rp.n8.cart-link/1", &link, &books, &LinkBooks::onAcquired,
                                          &LinkBooks::onReleased}};
    rp::box::DeviceHost host;
    host.init(links, 1, nullptr, &ignoreLog);
    const rp_host* h = host.host();
    const auto*    devices = static_cast<const rp_host_devices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
    REQUIRE(devices);

    CHECK(devices->acquire(devices->ctx, "n8", "rp.n8.cart-link/1") == &link);
    CHECK(books.acquired == 1);
    // An unknown name, another interface (another version of it too) and a null name or interface: null, no hook.
    CHECK(devices->acquire(devices->ctx, "pad", "rp.n8.cart-link/1") == nullptr);
    CHECK(devices->acquire(devices->ctx, "n8", "rp.n8.cart-link/2") == nullptr);
    CHECK(devices->acquire(devices->ctx, nullptr, "rp.n8.cart-link/1") == nullptr);
    CHECK(devices->acquire(devices->ctx, "n8", nullptr) == nullptr);
    CHECK(books.acquired == 1);
    // A pointer the host never installed, and a null, are ignored; the installed link's release reaches its hook.
    devices->release(devices->ctx, &stray);
    devices->release(devices->ctx, nullptr);
    CHECK(books.released == 0);
    devices->release(devices->ctx, &link);
    CHECK(books.released == 1);
    // No midi interface was given, and no other interface exists.
    CHECK(h->get_interface(h, RP_DEVICE_MIDI_IN_INTERFACE) == nullptr);
    CHECK(h->get_interface(h, "rp.host.jobs/1") == nullptr);
    CHECK(h->get_interface(h, nullptr) == nullptr);
    CHECK(h->alloc == nullptr);
    CHECK(h->free == nullptr);
}

TEST_CASE("staged MIDI reaches the box's plan at the frame its arrival falls on, and the plan keeps what the bridge "
          "always forwarded",
          "[device-mode][box]") {
    CartRecord cart{};
    Box        box;
    box.load(rp_devtest_cart_device_mode(&cart));
    REQUIRE(gLines.size() == 5);
    CHECK(gLines[0].starts_with("[host] plan loaded: 3 nodes, "));
    CHECK(gLines[0].ends_with(" of " + std::to_string(kHostArenaBytes) + " arena bytes, 48000 Hz, 256 frames a block"));
    CHECK(gLines[1].starts_with("[host]   node in (rp.device.midi-in), "));
    CHECK(gLines[2].starts_with("[host]   node policy (rp.bliptoaster.midi-policy), "));
    CHECK(gLines[3].starts_with("[host]   node nes (rp.n8.nes), "));
    CHECK(gLines[4].starts_with("[host]   pack bliptoaster, "));

    // A note-on, channel pressure, a clock and poly aftertouch (which the plan drops), a control change, the box's own
    // command (which the staging's hook takes) and a note-off.
    box.arrive({0x90, 0x45, 0x7F});
    box.arrive({0xD0, 0x20, 0xF8, 0xA0, 0x45, 0x10});
    box.arrive({0xB0, 0x07, 0x64});
    box.arrive({0xF0, 0x7D, 0x50, 0x02, 0xF7});
    box.arrive({0x80, 0x45, 0x00});
    REQUIRE(box.pass(1'000));
    // The first block made the policy node's port pools: the pack's heap, said once.
    REQUIRE(gLines.size() == 6);
    CHECK(gLines[5].starts_with("[host]   pack bliptoaster: heap "));
    CHECK(gLines[5].find(" of " + std::to_string(rp_script_bliptoaster.heap_bytes * rp::mqjs::kDeviceHalves / 2) +
                         " bytes in use after the first block") != std::string::npos);
    // 20 bytes at 320 us: the note-on's last byte at 960 us (frame 46), the control change's at 3,840 (184), the
    // note-off's at 6,400 (307). 7,400 us have passed, 355 frames: a block of 256, whose last frame the note-off is at.
    CHECK(pushedLines(cart) == "46: 90 45 7f\n184: b0 7 64\n255: 80 45 0\n");
    CHECK(box.sysex.count == 1);

    // A wait past the carry's bound: what arrives meanwhile goes to the next block, at that block's last frame (it is
    // behind the clock), the time past kCarryBlocks blocks is dropped, and the blocks after it run empty until the
    // carry is gone.
    cart.count = 0;
    box.clock.us += 20'000;  // the main loop held elsewhere: an N8 call
    box.arrive({0x90, 0x48, 0x7F});
    REQUIRE(box.pass());
    CHECK(pushedLines(cart) == "255: 90 48 7f\n");
    // 28,360 us since the start, 1,361 frames due, 256 run: 1,105 owed, 81 of them past the bound's 1,024 and dropped;
    // of the rest, the block that took the message ran 256 and 768 remain, three blocks.
    CHECK(box.driver.skipped() == 81);
    int empty = 0;
    while (box.pass()) ++empty;
    CHECK(empty == 3);
    CHECK(cart.count == 1);
    CHECK(box.plan.plan()->faults.load() == 0);
    CHECK(box.plan.plan()->drops.load() == 0);
    unloadPlan(box.plan.plan());
}

namespace {

// What the box's cart link wrote, as a Writer's fake FIFO and register take it.
struct FifoLog {
    std::vector<Bytes>         writes;
    std::vector<std::uint32_t> messages, firstUs;
    std::vector<int>           volumes;
    bool                       fifoFails = false, volumeFails = false;

    static bool fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t m, std::uint32_t first) {
        auto& self = *static_cast<FifoLog*>(ctx);
        self.writes.emplace_back(d, d + n);
        self.messages.push_back(m);
        self.firstUs.push_back(first);
        return !self.fifoFails;
    }
    std::vector<bool>          again;  // each volume write's retry flag
    static bool volume(void* ctx, int value, bool retry) {
        auto& self = *static_cast<FifoLog*>(ctx);
        self.volumes.push_back(value);
        self.again.push_back(retry);
        return !self.volumeFails;
    }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, &volume}; }
    Bytes stream() const {
        Bytes all;
        for (const Bytes& w : writes) all.insert(all.end(), w.begin(), w.end());
        return all;
    }
};

// When each message arrived: frame f arrived at 1000 + f us, and the plan's own at `now`.
struct LinkClock {
    std::uint32_t        now = 777;
    static std::uint32_t nowFn(void* ctx) { return static_cast<LinkClock*>(ctx)->now; }
    static bool          arrival(void*, std::uint32_t frame, const std::uint8_t*, std::size_t, std::uint32_t* us) {
        if (frame == 0xFFFF) return false;
        *us = 1000 + frame;
        return true;
    }
    rp::box::BoxCartLink::Clock clock() { return {this, &nowFn, &arrival}; }
};

struct Link {
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    LinkClock                                      clock;
    FifoLog                                        log;
    Link() { link.setClock(clock.clock()); }
    bool push(std::uint32_t frame, const Bytes& b) { return link.push(frame, b.data(), b.size(), false, 48000.0); }
};

Bytes note(std::uint8_t status, std::uint8_t key, std::uint8_t velocity) { return {status, key, velocity}; }

}  // namespace

TEST_CASE("the box's cart link refuses while no game runs, batches up to 192 bytes, and splits only a long message",
          "[device-mode][box]") {
    Link l;
    SECTION("no game running") {
        CHECK_FALSE(l.push(0, note(0x90, 60, 100)));
        CHECK(l.link.counts().notReady == 1);
        CHECK(l.link.counts().received == 1);
        CHECK(l.link.dropped() == 1);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.writes.empty());
    }
    SECTION("whole messages back to back, 192 bytes a write") {
        l.link.setConnected(true);
        Bytes in;
        for (std::uint32_t i = 0; i < 100; ++i) {
            const Bytes m = note(0x90, static_cast<std::uint8_t>(i), 64);
            REQUIRE(l.push(i, m));
            in.insert(in.end(), m.begin(), m.end());
        }
        l.link.drain(l.log.writer(), 0);
        REQUIRE(l.log.writes.size() == 2);
        CHECK(l.log.writes[0].size() == 192);
        CHECK(l.log.messages == std::vector<std::uint32_t>{64, 36});
        CHECK(l.log.firstUs == std::vector<std::uint32_t>{1000, 1064});  // each write's first message's arrival
        CHECK(l.log.stream() == in);
        CHECK(l.link.counts().forwarded == 100);
        CHECK(l.link.counts().writes == 2);
        CHECK(l.link.counts().maxBatch == 64);
        CHECK(l.link.queued() == 0);
    }
    SECTION("a message that does not fit waits for the next write, whole") {
        l.link.setConnected(true);
        REQUIRE(l.push(0, {0xC0, 0x05}));
        for (std::uint32_t i = 1; i <= 64; ++i) REQUIRE(l.push(i, note(0x90, 60, 64)));
        l.link.drain(l.log.writer(), 0);
        REQUIRE(l.log.writes.size() == 2);
        CHECK(l.log.writes[0].size() == 2 + 63 * 3);
        CHECK(l.log.writes[1].size() == 3);
    }
    SECTION("a SysEx longer than a write is split, and counted in the write that carries its end") {
        l.link.setConnected(true);
        Bytes sysex(300, 0x21);
        sysex.front() = 0xF0;
        sysex.back() = 0xF7;
        Bytes in = note(0x90, 60, 64);
        in.insert(in.end(), sysex.begin(), sysex.end());
        const Bytes after = note(0x80, 60, 0);
        in.insert(in.end(), after.begin(), after.end());
        REQUIRE(l.push(1, note(0x90, 60, 64)));
        REQUIRE(l.push(2, sysex));
        REQUIRE(l.push(3, after));
        l.link.drain(l.log.writer(), 0);
        REQUIRE(l.log.writes.size() == 2);
        CHECK(l.log.writes[0].size() == 192);
        CHECK(l.log.writes[1].size() == 300 - 189 + 3);
        CHECK(l.log.messages == std::vector<std::uint32_t>{1, 2});
        CHECK(l.log.firstUs == std::vector<std::uint32_t>{1001, 1002});  // the second starts inside the SysEx
        CHECK(l.log.stream() == in);
        CHECK(l.link.counts().sysex == 1);
        CHECK(l.link.counts().forwarded == 3);
    }
    SECTION("a failed write counts what it carried as failed") {
        l.link.setConnected(true);
        REQUIRE(l.push(0, note(0x90, 60, 64)));
        REQUIRE(l.push(1, note(0x80, 60, 0)));
        l.log.fifoFails = true;
        l.link.drain(l.log.writer(), 0);
        CHECK(l.link.counts().failed == 2);
        CHECK(l.link.counts().forwarded == 0);
        CHECK(l.link.queued() == 0);
    }
    SECTION("a full ring refuses, counted, and a disconnect drops what is queued") {
        l.link.setConnected(true);
        for (std::uint32_t i = 0; i < rp::box::BoxCartLink::kMessages; ++i) REQUIRE(l.push(i, {0xF8}));
        CHECK_FALSE(l.push(0, {0xF8}));
        CHECK(l.link.counts().full == 1);
        l.link.setConnected(false);
        CHECK(l.link.queued() == 0);
        CHECK(l.link.counts().notReady == rp::box::BoxCartLink::kMessages);
        l.link.setConnected(true);
        Bytes big(rp::box::BoxCartLink::kBytes / 2, 0x10);
        big.front() = 0xF0;
        big.back() = 0xF7;
        CHECK(l.push(0, big));
        CHECK(l.push(0, big));
        CHECK_FALSE(l.push(0, {0xF8}));  // the bytes are full
        CHECK(l.link.counts().full == 2);
    }
}

TEST_CASE("the box's cart link writes the expansion volume at every connect and on a change, and queues released "
          "note-offs",
          "[device-mode][box]") {
    Link l;
    SECTION("the volume") {
        l.link.setConnected(true);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes.empty());  // -1: the register is the cart's
        l.link.setExpansionVolume(128);
        l.link.drain(l.log.writer(), 0);
        l.link.setExpansionVolume(128);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes == std::vector<int>{128});
        l.link.setExpansionVolume(300);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes == std::vector<int>{128, 255});
        l.link.setConnected(false);
        l.link.setExpansionVolume(64);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes.size() == 2);  // not while no game runs
        l.link.setConnected(true);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes == std::vector<int>{128, 255, 64});
        l.link.setConnected(false);
        l.link.setConnected(true);  // a fresh game: the N8 OS's register again, so the value again
        l.log.volumeFails = true;
        l.link.drain(l.log.writer(), 10'000);
        l.link.drain(l.log.writer(), 10'500);
        CHECK(l.log.volumes.size() == 4);  // a failed write waits a second
        l.log.volumeFails = false;
        l.link.drain(l.log.writer(), 11'000);
        l.link.drain(l.log.writer(), 11'001);
        CHECK(l.log.volumes == std::vector<int>{128, 255, 64, 64, 64});
        CHECK(l.log.again == std::vector<bool>{false, false, false, false, true});  // the last a retry
    }
    SECTION("the last holder's release leaves the register to the cart") {
        l.link.acquire();
        l.link.acquire();
        l.link.setExpansionVolume(140);
        l.link.release();
        CHECK(l.link.expansionVolume() == 140);
        l.link.release();
        CHECK(l.link.expansionVolume() == -1);
        CHECK(l.link.holders() == 0);
        l.link.setConnected(true);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.volumes.empty());
    }
    SECTION("released notes are note-offs behind what was pushed") {
        rp::n8::CartNotes notes;
        notes.set(0 * 128 + 60);
        notes.set(3 * 128 + 64);
        l.link.releaseNotes(notes);
        CHECK(l.link.counts().notReady == 2);  // no game running: dropped
        l.link.setConnected(true);
        REQUIRE(l.push(0, note(0x91, 10, 20)));
        l.link.releaseNotes(notes);
        l.link.drain(l.log.writer(), 0);
        CHECK(l.log.stream() == Bytes{0x91, 10, 20, 0x80, 60, 0, 0x83, 64, 0});
        CHECK(l.log.firstUs == std::vector<std::uint32_t>{1000});
        CHECK(l.link.counts().released == 2);
        CHECK(l.link.counts().received == 1);  // a release is no push
    }
}

TEST_CASE("the box's cart link tells the node's own note-offs from what the plan was handed", "[device-mode][box]") {
    Link l;
    l.link.setConnected(true);
    REQUIRE(l.push(0, note(0x90, 60, 100)));
    REQUIRE(l.push(0, note(0x90, 62, 100)));
    REQUIRE(l.push(0, note(0x80, 62, 0)));
    l.link.drain(l.log.writer(), 0);
    l.link.setConnected(false);
    CHECK_FALSE(l.push(0, note(0x80, 60, 0)));  // refused while 60 sounds: rp.n8.nes now owes it
    CHECK_FALSE(l.push(0, note(0x90, 64, 0)));  // a note-off (velocity 0) for a note not sounding: owed nothing
    l.link.setConnected(true);
    REQUIRE(l.push(0, note(0x80, 60, 0)));  // the node's payment, at the next connected block
    CHECK(l.link.counts().made == 1);
    REQUIRE(l.push(5, note(0x80, 60, 0)));  // the player's own release, later: handed, not made
    CHECK(l.link.counts().made == 1);
    CHECK(l.link.counts().received == 7);
    CHECK(l.link.counts().notReady == 2);
}

namespace {

// The box's link as the device builds it, its clock the box's: the staging's arrivals, the fake clock's now.
std::uint32_t boxNow(void* ctx) { return static_cast<std::uint32_t>(static_cast<Box*>(ctx)->clock.us); }
bool boxArrival(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us) {
    return static_cast<Box*>(ctx)->staging->arrival(frame, data, size, us);
}

BoxCounts boxCounts() {
    BoxCounts c{};
    rp_devtest_box_counts_device_mode(&c);
    return c;
}

std::string writeLines(const BoxRecord& r) {
    std::ostringstream o;
    for (std::uint32_t i = 0; i < r.writeCount && i < BoxRecord::kWrites; ++i) {
        o << r.writes[i].messages << ":";
        for (std::uint32_t k = 0; k < r.writes[i].size; ++k)
            o << " " << std::hex << int(r.writes[i].bytes[k]) << std::dec;
        o << "\n";
    }
    return o.str();
}

}  // namespace

TEST_CASE("the box's pass: UART bytes to FIFO writes through its plan and its link, allocating nothing, counted at "
          "the plan's edges",
          "[device-mode][box]") {
    auto box = std::make_unique<Box>();
    auto record = std::make_unique<BoxRecord>();
    record->clockCtx = box.get();
    record->now = &boxNow;
    record->arrival = &boxArrival;
    void* link = rp_devtest_box_link_device_mode(record.get());
    box->load(link, &rp_devtest_box_acquired_device_mode, &rp_devtest_box_released_device_mode);
    CHECK(boxCounts().holders == 1);
    auto drain = [&] { rp_devtest_box_drain_device_mode(static_cast<std::uint32_t>(box->clock.us / 1000)); };

    // Before a game runs: refused, counted not ready, nothing written; rp.n8.nes gives the link its volume at once.
    box->arrive({0x90, 0x45, 0x7F});
    REQUIRE(box->pass(1'000));
    drain();
    CHECK(record->writeCount == 0);
    CHECK(boxCounts().notReady == 1);
    CHECK(boxCounts().volume == 128);

    // The game is up: the volume at the connect, then a pass's messages in one write, timed from the first one's
    // arrival. The plan drops the channel pressure, the clock and the song position; the box's command goes nowhere.
    rp_devtest_box_connect_device_mode(true);
    box->arrive({0x90, 0x45, 0x7F});
    const std::uint32_t noteAt = static_cast<std::uint32_t>(box->clock.us);
    box->arrive({0xD0, 0x20, 0xF8, 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0xF0, 0x7D, 0x50, 0x02, 0xF7});
    box->arrive({0xB0, 0x07, 0x7F, 0xF2, 0x10, 0x20});
    // The disconnect below happens with ch1's A held, its note-off arriving while no game runs.
    box->ringEnd = box->ring.size();
    box->arrive({0x80, 0x45, 0x00});

    rp::test::beginCountingAllocations();
    // The pass up to the note-off, then the drain: one write.
    const bool ranOne = box->pass(500);
    drain();
    const BoxCounts afterOne = boxCounts();
    // A disconnect (the NES's power cut), the note-off refused and owed, a reconnect and a pass with nothing staged,
    // in which rp.n8.nes pays the note-off it owes.
    rp_devtest_box_connect_device_mode(false);
    box->ringEnd = SIZE_MAX;
    const bool ranTwo = box->pass(500);
    drain();
    rp_devtest_box_connect_device_mode(true);
    const bool ranThree = box->pass(500);
    drain();
    const auto counted = rp::test::endCountingAllocations();

    CHECK(ranOne);
    CHECK(ranTwo);
    CHECK(ranThree);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(writeLines(*record) == "3: 90 45 7f f0 7e 7f 6 1 f7 b0 7 7f\n1: 80 45 0\n");
    CHECK(record->writes[0].firstUs == noteAt);
    CHECK(record->volumeCount == 2);  // at each connect
    CHECK(record->volumes[0] == 128);
    CHECK(record->volumes[1] == 128);
    CHECK(afterOne.forwarded == 3);
    CHECK(afterOne.sysex == 1);

    const BoxCounts c = boxCounts();
    const auto&     in = box->staging->counts();
    CHECK(in.handed == 1 + 6 + 1);  // a note-on refused, six staged (the command taken), the note-off refused
    CHECK(in.consumed == 1);
    CHECK(c.received == 1 + 3 + 1 + 1);
    CHECK(c.made == 1);  // the note-off rp.n8.nes owed, paid at the reconnect
    CHECK(c.notReady == 2);
    CHECK(c.forwarded == 4);
    // The box's `filtered` (BoxStats.hpp, main.cpp's arithmetic): staged minus what the link was handed from the
    // plan's input: the three the plan dropped.
    rp::box::BoxCartLink::Counts out;
    out.received = c.received;
    out.made = c.made;
    CHECK(out.fromPlanInput() == c.fromPlanInput);
    CHECK(rp::box::filtered(in, out) == 3);
    CHECK(in.untaken == 0);
    CHECK(box->plan.plan()->drops.load() == 0);
    unloadPlan(box->plan.plan());
    CHECK(boxCounts().holders == 0);
    CHECK(boxCounts().volume == -1);
}

TEST_CASE("the box's plan.json exports to its golden's bytes, as the tool writes it for the firmware, and fits the "
          "box's arena",
          "[device-mode][box]") {
    const Bytes& blob = boxBlob();
    const std::filesystem::path golden = std::filesystem::path(RP_DEVICE_MODE_GOLDEN_DIR) / "box-plan.hex";
    if (const char* update = std::getenv("RP_PLAN_BLOB_UPDATE"); update && std::string(update) == "1") {
        std::ofstream(golden, std::ios::binary) << hexOf(blob);
        WARN("recorded " << golden.string());
    }
    CHECK(readFile(golden) == hexOf(blob));

    // The tool, in a process of its own, as pico/n8-host/CMakeLists.txt runs it: the same bytes, and the header form.
    const auto dir = std::filesystem::temp_directory_path() / ("rp-box-plan-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    struct Remove {  // the directory goes with the case, a failed REQUIRE included
        const std::filesystem::path& dir;
        ~Remove() { std::error_code ignored; std::filesystem::remove_all(dir, ignored); }
    } removeDir{dir};
    const std::string tool = RP_EXPORT_PLAN_TOOL;
    const std::string flags = " --arena " + std::to_string(kArenaBytes) + " --sample-rate " + std::to_string(kRate) +
                              " --max-frames " + std::to_string(kMaxFrames);
    const std::string packages = std::string(" --packages ") + RP_BOX_PACKAGES;
    auto run = [&](const std::string& out, const std::string& extra) {
        std::string command = "\"" + tool + "\" \"" + std::string(RP_BOX_PLAN) + "\" \"" + (dir / out).string() +
                              "\"" + flags + extra + " > \"" + (dir / "stdout.txt").string() + "\" 2>&1";
#ifdef _WIN32
        command = "\"" + command + "\"";  // cmd /c drops a command line's outer quotes when it starts with one
#endif
        return std::system(command.c_str());
    };
    REQUIRE(run("plan.bin", packages) == 0);
    const std::string bytes = readFile(dir / "plan.bin");
    CHECK(Bytes(bytes.begin(), bytes.end()) == blob);
    REQUIRE(run("plan.hpp", packages + " --header kBuiltInPlan") == 0);
    const std::string header = readFile(dir / "plan.hpp");
    CHECK(header.find("alignas(4) inline constexpr std::uint8_t kBuiltInPlan[] = {") != std::string::npos);
    const std::string estimate = std::to_string(boxExport().size.bytes);
    CHECK(header.find("inline constexpr std::size_t kBuiltInPlanArenaEstimate = " + estimate + ";") !=
          std::string::npos);
    // Held to an image's own packages, the tool refuses a plan naming a type that image does not register, as the
    // firmware's build runs it: plan.json's rp.device.midi-in, for an image without device-io; and a name it does not
    // carry, as a usage error.
    CHECK(run("refused.bin", " --packages core,n8") != 0);
    CHECK(readFile(dir / "stdout.txt").find("unknown-type in: no node type \"rp.device.midi-in\" is registered") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir / "refused.bin"));
    CHECK(run("refused.bin", " --packages core,nope") != 0);
    CHECK(readFile(dir / "stdout.txt").find("--packages names \"nope\", which is no package the tool carries") !=
          std::string::npos);

    // The export holds the plan to the box's arena at the box's width, its pack's memory as the box takes it; loaded
    // here as the box loads it, it takes what this width's sizing measures, which differs from the export's by the
    // pack's memory alone.
    CartRecord cart{};
    Box        box;
    box.load(rp_devtest_cart_device_mode(&cart));
    const ExportResult r = boxExport();
    CHECK(r.size.bytes <= kArenaBytes);
    CHECK(r.size.packages == 1);
    CHECK(r.size.packageBytes == rp::mqjs::boxPackageBytes(rp_script_bliptoaster));
    PlanSize      here;
    PlanLoadError err{};
    REQUIRE(sizePlan(blob, boxTypes(), 0, &here, &err));
    CHECK(box.plan.plan()->bytes == here.bytes);
    CHECK(here.bytes - here.packageBytes == r.size.bytes - r.size.packageBytes);
    CHECK(here.packageBytes == rp::mqjs::packageBytes(rp_script_bliptoaster));
    CHECK(box.plan.plan()->nodes.size() == 3);
    unloadPlan(box.plan.plan());
}

TEST_CASE("a built-in plan the loader refuses is said, and runs nothing; a node that faults is said once",
          "[device-mode][box]") {
    rp::box::DeviceHost host;
    host.init(nullptr, 0, nullptr, &ignoreLog);
    FakeClock              clock{0};
    rp::box::ElapsedDriver driver;
    driver.start(&FakeClock::read, &clock, kRate, kMaxFrames);
    Arena arena(kArenaBytes);

    SECTION("refused") {
        Bytes blob = boxBlob();
        blob[blob.size() - 1] ^= 0xFF;  // the CRC
        rp::box::BuiltInPlan plan;
        gLines.clear();
        CHECK_FALSE(plan.load(blob, arena.span(kArenaBytes), boxTypes(), host.host(), &collect));
        REQUIRE(gLines.size() == 2);
        CHECK(gLines[0].starts_with("[host] the built-in plan was refused: plan-crc -: "));
        CHECK(gLines[1] == "[host] no plan runs, and nothing is forwarded");
        clock.us = 100'000;
        CHECK_FALSE(plan.run(driver));
        CHECK(plan.plan() == nullptr);
    }
    SECTION("a fault") {
        NodeRegistry registry;
        std::string  why;
        REQUIRE(registry.addPackage(rp::graph::devtest::faultPackageDesktop(), &why));
        const std::vector<const rp_package*> types{rp_devtest_fault_package_device_mode()};
        ExportOptions o;
        o.arenaBytes = kArenaBytes;
        const ExportResult r = exportPlan(
            R"({"nodes":[{"id":"f","type":"rp.devtest.fault","settings":{"at":"process"}}],"wires":[]})", registry,
            types, o);
        REQUIRE(r.ok());
        rp::box::BuiltInPlan plan;
        gLines.clear();
        REQUIRE(plan.load(r.blob, arena.span(kArenaBytes), types, host.host(), &collect));
        CHECK_FALSE(plan.faulted());
        gLines.clear();
        clock.us = 10'000;
        REQUIRE(plan.run(driver));
        REQUIRE(gLines.size() == 1);
        CHECK(gLines[0] == "[host] node f (rp.devtest.fault) faulted: the executor silences it from here on");
        CHECK(plan.faulted());  // what the box's LED shows
        clock.us = 20'000;
        REQUIRE(plan.run(driver));
        CHECK(gLines.size() == 1);
        CHECK(plan.plan()->faults.load() == 1);
        CHECK(plan.faulted());
        unloadPlan(plan.plan());
    }
    SECTION("a script node's fault is said with its words, cut to a line of the console") {
        NodeRegistry registry;
        std::string  why;
        REQUIRE(registry.addPackage(rp_scripttest_script_package(), &why));
        const std::vector<const rp_package*> types{rp_scripttest_script_package_device_mode()};
        constexpr std::size_t kScriptArena = std::size_t{1} << 20;  // the pack's memory, its context 384 KiB here
        ExportOptions o;
        o.arenaBytes = kScriptArena;
        // The node runs its first block (the export's trial load runs one), then throws at its second.
        const ExportResult r = exportPlan(
            R"({"nodes":[{"id":"t","type":"rp.scripttest.throws-later"}],"wires":[]})", registry, types, o);
        INFO((r.errors.empty() ? std::string() : r.errors[0].message));
        REQUIRE(r.ok());
        Arena                big(kScriptArena);
        rp::box::BuiltInPlan plan;
        REQUIRE(plan.load(r.blob, big.span(kScriptArena), types, host.host(), &collect));
        clock.us = 10'000;
        REQUIRE(plan.run(driver));
        gLines.clear();
        clock.us = 20'000;
        REQUIRE(plan.run(driver));
        REQUIRE(gLines.size() == 2);
        CHECK(gLines[0] == "[host] node t (rp.scripttest.throws-later) faulted: the executor silences it from here on");
        // The fault keeps 159 bytes of its words (rp.script.fault/1), and the console's line 159 of its own.
        const std::string words =
            "process threw: RangeError: the throws-later node threw on its second block, with words that run on past "
            "what a fault keeps of them and past what a line of the box's console holds, which cuts them short there";
        CHECK(gLines[1] == ("[host]   " + words).substr(0, 159));
        clock.us = 30'000;
        REQUIRE(plan.run(driver));
        CHECK(gLines.size() == 2);  // once
        unloadPlan(plan.plan());
    }
    SECTION("faults past the nodes the plan keeps books for are counted, not named") {
        NodeRegistry registry;
        std::string  why;
        REQUIRE(registry.addPackage(rp::graph::devtest::faultPackageDesktop(), &why));
        const std::vector<const rp_package*> types{rp_devtest_fault_package_device_mode()};
        constexpr std::uint32_t              kFaulting = rp::box::BuiltInPlan::kNodes + 2;
        std::string                          doc = R"({"nodes":[)";
        for (std::uint32_t i = 0; i < kFaulting; ++i)
            doc += std::string(i ? "," : "") + R"({"id":"f)" + std::to_string(i) +
                   R"(","type":"rp.devtest.fault","settings":{"at":"process"}})";
        doc += R"(],"wires":[]})";
        ExportOptions o;
        o.arenaBytes = kArenaBytes;
        const ExportResult r = exportPlan(doc, registry, types, o);
        REQUIRE(r.ok());
        rp::box::BuiltInPlan plan;
        gLines.clear();
        REQUIRE(plan.load(r.blob, arena.span(kArenaBytes), types, host.host(), &collect));
        gLines.clear();
        clock.us = 10'000;
        REQUIRE(plan.run(driver));
        REQUIRE(gLines.size() == rp::box::BuiltInPlan::kNodes + 1);
        CHECK(gLines.front() == "[host] node f0 (rp.devtest.fault) faulted: the executor silences it from here on");
        CHECK(gLines.back() == "[host] 2 more nodes faulted");
        CHECK(plan.plan()->faults.load() == kFaulting);
        clock.us = 20'000;
        REQUIRE(plan.run(driver));
        CHECK(gLines.size() == rp::box::BuiltInPlan::kNodes + 1);  // each once
        unloadPlan(plan.plan());
    }
}

TEST_CASE("the box's staging hands a kept message its own arrival, not a dropped one's at the same frame",
          "[device-mode][box]") {
    auto s = std::make_unique<rp::box::MidiStaging>();
    s->init(nullptr, nullptr, &noFrame, nullptr);  // every message at frame 0
    std::uint32_t at = 1'000;
    REQUIRE(feed(*s, {0xD0, 0x20}, at));  // channel pressure, its last byte at 1,320 (the plan drops it)
    REQUIRE(feed(*s, {0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00}, at));  // the note-on's last byte at 2,280, the off's 3,240
    rp_device_midi_message   m{};
    const rp_device_midi_in* in = s->interface();
    while (in->next(in->ctx, kMaxFrames, &m)) {
    }
    const Bytes   on{0x90, 0x3C, 0x64}, off{0x80, 0x3C, 0x00}, own{0x80, 0x40, 0x00};
    std::uint32_t us = 0;
    CHECK_FALSE(s->arrival(0, own.data(), own.size(), &us));  // a message the node made itself: none
    REQUIRE(s->arrival(0, on.data(), on.size(), &us));
    CHECK(us == 2'280);  // the note-on's own last byte, not the channel pressure's 1,320 ahead of it
    REQUIRE(s->arrival(0, off.data(), off.size(), &us));
    CHECK(us == 3'240);
    CHECK_FALSE(s->arrival(0, on.data(), on.size(), &us));  // each one answered once
    s->endBlock();
}

TEST_CASE("the box's commands get through whatever the plan does: a silenced midi-in, a plan with none, no plan",
          "[device-mode][box]") {
    // 300 note-ons, more than a block takes, then the box's own command: the staging must empty after every block,
    // whoever takes from it, or the command would never be parsed.
    Bytes wire;
    for (int i = 0; i < 300; ++i) wire.insert(wire.end(), {0x90, static_cast<std::uint8_t>(i % 128), 0x40});
    wire.insert(wire.end(), {0xF0, 0x7D, 0x50, 0x02, 0xF7});
    CartRecord cart{};
    auto       box = std::make_unique<Box>();
    std::uint32_t untaken = 0;
    auto run = [&] {
        box->arrive(wire);
        for (int i = 0; i < 64 && box->ringAt < box->ring.size(); ++i) box->pass(6'000);
        box->pass(6'000);
        CHECK(box->ringAt == box->ring.size());
        CHECK(box->sysex.count == 1);
        CHECK(box->staging->staged() == 0);
        untaken = box->staging->counts().untaken;
    };

    SECTION("rp.device.midi-in silenced by the executor for a fault") {
        box->load(rp_devtest_cart_device_mode(&cart));
        box->plan.plan()->nodes[0].faulted.store(true);  // the step's fault flag: the executor bypasses it
        run();
        CHECK(untaken == 300);
        CHECK(cart.count == 0);
        unloadPlan(box->plan.plan());
    }
    SECTION("a plan with no rp.device.midi-in") {
        NodeRegistry registry;
        std::string  why;
        REQUIRE(registry.addPackage(rp_core_package(), &why));
        ExportOptions o;
        o.arenaBytes = kArenaBytes;
        const ExportResult r =
            exportPlan(R"({"nodes":[{"id":"c","type":"rp.core.constant"}],"wires":[]})", registry, boxTypes(), o);
        REQUIRE(r.ok());
        box->loadBlob(r.blob, rp_devtest_cart_device_mode(&cart));
        run();
        CHECK(untaken == 300);
        unloadPlan(box->plan.plan());
    }
    SECTION("no plan (a refused one)") {
        box->loadBlob({}, rp_devtest_cart_device_mode(&cart));
        run();
        CHECK(untaken == 300);
    }
}

namespace {

// A cart's USB end for the volume's write and read-back alone: memWR is fire-and-forget (the port taking its bytes is
// the write), and memRD's reply is `reads` bytes of `value`.
struct VolumePort final : rp::n8::Transport {
    bool               takes = true;    // false: the port takes nothing, the write fails at the transport
    bool               answers = true;  // false: the read-back never comes
    std::uint8_t       value = 128;
    std::vector<Bytes> written;
    std::size_t        write(const std::uint8_t* d, std::size_t n) override {
        if (!takes) return 0;
        written.emplace_back(d, d + n);
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t) override {
        if (!answers) return 0;
        for (std::size_t i = 0; i < n; ++i) b[i] = value;
        return n;
    }
    void flushInput() override {}
};

}  // namespace

TEST_CASE("the box's volume write: done whatever it reads back, tried again only when the transport failed, each "
          "outcome said once",
          "[device-mode][box]") {
    VolumePort    port;
    rp::n8::Edio  edio(port);
    gLines.clear();
    SECTION("written, and it reads back what was written") {
        CHECK(rp::box::writeExpansionVolume(edio, 128, false, &collect));
        CHECK_FALSE(port.written.empty());
        CHECK(gLines == std::vector<std::string>{"[n8-host] cart link: expansion volume 128 written (reads back 128)"});
    }
    SECTION("written, and it reads back another value: done, not retried") {
        port.value = 129;
        CHECK(rp::box::writeExpansionVolume(edio, 128, false, &collect));
        CHECK(gLines ==
              std::vector<std::string>{"[n8-host] cart link: expansion volume 128 written, but reads back 129"});
    }
    SECTION("written, and no read-back comes: done") {
        port.answers = false;
        CHECK(rp::box::writeExpansionVolume(edio, 128, false, &collect));
        CHECK(gLines == std::vector<std::string>{"[n8-host] cart link: expansion volume 128 written (no read-back)"});
    }
    SECTION("the transport failed the write: false, said on the first try and not on a retry") {
        port.takes = false;
        CHECK_FALSE(rp::box::writeExpansionVolume(edio, 128, false, &collect));
        CHECK_FALSE(rp::box::writeExpansionVolume(edio, 128, true, &collect));
        // No "FAILED": the gate's boot legs fail on that word (loop.sh --fail), and a transient write is retried.
        CHECK(gLines ==
              std::vector<std::string>{
                  "[n8-host] cart link: expansion volume 128: write did not go through, trying again each second"});
    }
}

TEST_CASE("the reliable link's sender allocates nothing, as the box runs it: made, a full window, its re-sends and a "
          "re-sync",
          "[device-mode][box][n8-wire]") {
    using rp::N8ReliableSender;
    // The box's transport keeps what goes out in storage of its own and marks each frame sent as it leaves: an emitter
    // of five references, past the two a std::function holds without allocating.
    std::optional<N8ReliableSender> s;
    std::array<std::uint8_t, 4096>  wire{};
    std::size_t                     wireLen = 0;
    int                             framesOut = 0;
    std::int64_t                    now = 1'000'000'000;
    const auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        for (std::size_t i = 0; i < n && wireLen < wire.size(); ++i) wire[wireLen++] = d[i];
        if (seq >= 0) {
            ++framesOut;
            s->onSent(seq, now);
        }
    };
    N8ReliableSender::Timing t;
    t.resendDelayNs = 0;

    rp::test::beginCountingAllocations();
    s.emplace(t);  // made where the box makes it: inside a pass
    for (int i = 0; i < 48; ++i) {
        const std::uint8_t on[3] = {0x90, static_cast<std::uint8_t>(36 + i), 100};
        s->send(on, 3, emit);
    }
    const std::size_t full = s->outstanding();
    now += 100'000'000;  // nothing heard for the timeout: the whole window again
    s->poll(now, emit);
    const std::uint8_t ack[3] = {0xF5, 10, 20};  // ten arrived, and the ROM read 20: from 10 again, at once
    s->onBackChannel(ack, 3, now);
    s->poll(now, emit);
    const std::uint8_t sysex[5] = {0xF0, 0x7D, 0x42, 0x02, 0xF7};
    s->sendMessage(sysex, 5, emit);
    for (int i = 0; i < 10; ++i) {  // 39 out: the 49th outstanding frame gives the window up and re-syncs
        const std::uint8_t off[3] = {0x80, 60, 0};
        s->send(off, 3, emit);
    }
    const auto counted = rp::test::endCountingAllocations();

    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(full == 48);
    CHECK(s->resends() == 2);
    CHECK(s->framesResent() == 48 + 38);
    CHECK(s->framesSent() == 59);
    CHECK(framesOut == 59 + 48 + 38);
    CHECK(s->outstanding() == 1);
    // The re-sync, F4 <58>, ahead of the last frame (six bytes: F5 58 80 3C 00 <sum>).
    REQUIRE(wireLen >= 8);
    CHECK(wire[wireLen - 8] == N8ReliableSender::SYNC);
    CHECK(wire[wireLen - 7] == 58);
}

namespace {

// ---- the cart link's framed link and menu guard (docs/pico-box-framed-link-design.md section 5, item 4) ------------

using rp::n8::test::cat;
using rp::n8::test::frame;

const Bytes kProbeBytes(std::begin(rp::N8MenuGuard::kProbe), std::end(rp::N8MenuGuard::kProbe));

// A FIFO write as FakeN8's log records it, and its FIFO writes in order.
std::string fifoEntry(const Bytes& bytes) { return "fifo:" + retroplug::test::hex(bytes.data(), bytes.size()); }
std::vector<std::string> fifoWrites(retroplug::test::FakeN8& dev) {
    std::lock_guard<std::mutex> l(dev.mu);
    std::vector<std::string>    out;
    for (const std::string& e : dev.log)
        if (e.rfind("fifo:", 0) == 0) out.push_back(e);
    return out;
}

// The box's link over FakeN8 (packages/n8/native/test/FakeN8.hpp, the cart model the desktop's guard is tested on),
// through the Writer the box gives it: a FIFO write is Edio's fifoWR over a FakeN8Port, the back-channel is that
// port's input read without waiting, as the box's read(..., 0) is, and the link's clock is the case's own, so the
// guard's 100 ms, 250 ms and 1 s pass as a case moves it. FakeN8 answers at once (its delays stay 0: they would run on
// the wall clock), so no case sleeps. A game runs from the start (FakeN8 starts at its menu otherwise).
struct CartRig {
    retroplug::test::FakeN8                        dev;
    std::unique_ptr<retroplug::test::FakeN8Port>   port = std::make_unique<retroplug::test::FakeN8Port>(dev);
    rp::n8::Edio                                   edio{*port};
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;  // never 0, which the guard reads as "no probe"

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<CartRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<CartRig*>(ctx)->us; }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        return static_cast<CartRig*>(ctx)->edio.fifoWR(d, n).ok();
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        return static_cast<CartRig*>(ctx)->port->read(d, n, 0);
    }

    CartRig() {
        dev.menu = false;
        link.setClock({this, &now, nullptr, &monotonic});
    }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    // One pass's drain, `ms` after the last.
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
};

// A cart whose back-channel the case writes itself, for what FakeN8 never says on its own (an ack cut in two), and
// which takes every FIFO write.
struct ScriptedRig {
    std::vector<Bytes>                             writes;
    Bytes                                          back;  // what the cart has said and the link has not read
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<ScriptedRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<ScriptedRig*>(ctx)->us; }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        static_cast<ScriptedRig*>(ctx)->writes.emplace_back(d, d + n);
        return true;
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        auto&             self = *static_cast<ScriptedRig*>(ctx);
        const std::size_t k = n < self.back.size() ? n : self.back.size();
        for (std::size_t i = 0; i < k; ++i) d[i] = self.back[i];
        self.back.erase(self.back.begin(), self.back.begin() + static_cast<std::ptrdiff_t>(k));
        return k;
    }

    ScriptedRig() { link.setClock({this, &now, nullptr, &monotonic}); }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
    // Connected, and the probe's sync answered as BlipToaster answers it.
    void connectFramed() {
        link.setConnected(true);
        drain();
        back = {0xF5, 0x00, 0x7F};
        drain();
    }
};

// BlipToaster's end of the link as the link's ROM model plays it (LinkRomModel.hpp: the rules the sender's own soak is
// held to), behind a wire that loses bytes as an NTSC console's DMC fetch does, in bursts of 1 to 3, once `lossy`:
// FakeN8 models intact traffic only. Its FIFO runs empty after each write, so it acks then, when anything changed.
// The losses come from a fixed LCG (N8Link::setTestLossPermille's), not a <random> distribution, whose draws differ
// between standard libraries: the same bytes are lost on every platform, so a failure reproduces anywhere.
struct LossyRig {
    rp::n8::test::RomModel                         rom;
    std::uint32_t                                  rng = 0x2545F491u;
    bool                                           lossy = false;
    bool                                           failNext = false;  // the next FIFO write delivers nothing and fails
    int                                            lost = 0;
    Bytes                                          back;
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<LossyRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<LossyRig*>(ctx)->us; }
    std::uint32_t        draw() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        auto& self = *static_cast<LossyRig*>(ctx);
        if (self.failNext) {
            self.failNext = false;
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (self.lossy && self.draw() % 1000 < 15) {
                const int k = 1 + static_cast<int>(self.draw() % 3);
                self.lost += k;
                i += static_cast<std::size_t>(k) - 1;
                continue;
            }
            self.rom.byte(d[i]);
        }
        Bytes ack;
        if (self.rom.ack(ack)) self.back.insert(self.back.end(), ack.begin(), ack.end());
        return true;
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        auto&             self = *static_cast<LossyRig*>(ctx);
        const std::size_t k = n < self.back.size() ? n : self.back.size();
        for (std::size_t i = 0; i < k; ++i) d[i] = self.back[i];
        self.back.erase(self.back.begin(), self.back.begin() + static_cast<std::ptrdiff_t>(k));
        return k;
    }

    LossyRig() { link.setClock({this, &now, nullptr, &monotonic}); }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
};

// The device-mode link's 64-bit clock: the box's fake clock, as main.cpp hands its link time_us_64.
std::uint64_t boxNow64(void* ctx) { return static_cast<Box*>(ctx)->clock.us; }
// What the cart says on the device-mode link's back-channel, for its next drain to read.
void say(BoxRecord& r, std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) r.back[r.backCount++] = b;
}

}  // namespace

TEST_CASE("the box's link probe at a connect: framed for BlipToaster, raw for a ROM without the link",
          "[device-mode][box][box-link]") {
    CartRig     r;
    const Bytes on = note(0x90, 60, 100);
    SECTION("BlipToaster acknowledges the sync: framed, and the message goes out as a frame") {
        r.dev.linkRom = true;
        r.link.setConnected(true);
        r.drain();
        CHECK(r.link.framed());
        REQUIRE(r.push(on));
        r.drain();
        CHECK(r.dev.applied == std::vector<Bytes>{on});
        CHECK(fifoWrites(r.dev) ==
              std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(cat({{0xF4, 0x00}, frame(0, on)}))});
        CHECK(r.link.counts().forwarded == 1);
    }
    SECTION("a ROM without the link says nothing: raw after 100 ms, the message waiting in the ring until then") {
        r.link.setConnected(true);
        r.drain();  // the sync
        REQUIRE(r.push(on));
        r.drain(98);
        CHECK(r.link.queued() == 1);
        CHECK_FALSE(r.link.framed());
        r.drain(1);  // 99 ms
        CHECK(r.link.queued() == 1);
        r.drain(1);  // 100 ms: raw, the guard's first probe ahead of the message
        CHECK(r.link.queued() == 0);
        CHECK_FALSE(r.link.framed());
        CHECK(fifoWrites(r.dev) ==
              std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(cat({kProbeBytes, on}))});
    }
    SECTION("a link the box may not probe (N8_BOX_LINK=0) streams raw at once, guarded") {
        r.dev.linkRom = true;
        r.link.setLinkAllowed(false);
        r.link.setConnected(true);
        REQUIRE(r.push(on));
        r.drain();
        CHECK_FALSE(r.link.framed());
        CHECK(fifoWrites(r.dev) == std::vector<std::string>{fifoEntry(cat({kProbeBytes, on}))});
        CHECK(r.dev.syncs == 0);
    }
}

TEST_CASE("the box's link adds no pass of delay: a message pushed in a pass goes out in that pass's drain",
          "[device-mode][box][box-link]") {
    CartRig r;
    SECTION("framed") {
        r.dev.linkRom = true;
        r.link.setConnected(true);
        r.drain();
        REQUIRE(r.link.framed());
        for (std::uint8_t i = 0; i < 10; ++i) {
            REQUIRE(r.push(note(0x90, static_cast<std::uint8_t>(60 + i), 100)));
            r.drain();
            CHECK(r.dev.applied.size() == i + 1u);
            CHECK(r.link.counts().forwarded == i + 1u);
        }
        CHECK(r.link.outstanding() <= 1);  // the last frame's ack is read at the next drain
    }
    SECTION("raw") {
        r.link.setConnected(true);
        r.drain();
        r.drain(100);  // raw, and the guard's first probe
        REQUIRE_FALSE(r.link.framed());
        for (std::uint8_t i = 0; i < 10; ++i) {
            const Bytes on = note(0x90, static_cast<std::uint8_t>(60 + i), 100);
            REQUIRE(r.push(on));
            r.drain();
            CHECK(fifoWrites(r.dev).back() == fifoEntry(on));
            CHECK(r.link.counts().forwarded == i + 1u);
        }
    }
}

TEST_CASE("the box's framed link frames no more than its window in one drain, and the rest goes out once acks come",
          "[device-mode][box][box-link]") {
    // A backlog in one pass (the ring after a guard hold, a block after the loop was held): framed to empty, its 49th
    // frame would make the sender give its window up and re-sync, and a frame lost before that would be gone for good.
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    REQUIRE(r.dev.syncs == 1);  // the connect's probe
    std::vector<Bytes> sent;
    for (std::uint8_t i = 0; i < 100; ++i) {
        sent.push_back(note(0x90, i, 100));
        REQUIRE(r.push(sent.back()));
    }
    r.drain();
    CHECK(r.link.outstanding() == rp::N8ReliableSender::kWindow - 1);
    CHECK(r.link.queued() == 100 - (rp::N8ReliableSender::kWindow - 1));
    CHECK(r.dev.syncs == 2);  // and the sender's own, before its first frame: no give-up's re-sync
    for (int k = 0; k < 4 && r.link.queued() != 0; ++k) r.drain();  // each reads the acks, then frames more
    r.drain();                                                        // the last acks
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK(r.dev.syncs == 2);
    CHECK(r.dev.applied == sent);
    CHECK(r.link.counts().forwarded == 100);
}

TEST_CASE("the box's framed link through a wire that loses bytes: every message arrives once, in order",
          "[device-mode][box][box-link]") {
    LossyRig r;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    r.lossy = true;
    // 2000 messages (notes, CCs, the odd SysEx), three a sequencer step, four 1 ms passes a step.
    std::vector<Bytes> sent;
    for (int i = 0; i < 2000; ++i) {
        Bytes m;
        switch (i % 7) {
            case 3: m = {0xB1, std::uint8_t(i & 0x7F), std::uint8_t((i * 3) & 0x7F)}; break;
            case 6: m = {0xF0, 0x7D, 0x42, 0x02, 0xF7}; break;
            default: m = note(std::uint8_t(0x90 | (i % 4)), std::uint8_t(36 + i % 40), 100); break;
        }
        sent.push_back(m);
        REQUIRE(r.push(m));
        if (i % 3 == 2)
            for (int k = 0; k < 4; ++k) r.drain();
    }
    for (int k = 0; k < 4000 && (r.link.queued() != 0 || r.link.outstanding() != 0); ++k) r.drain();
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK(r.lost > 0);                         // the wire really did lose bytes
    CHECK(r.link.counts().resends > 0);        // and the link repaired them
    CHECK(r.link.counts().resent >= r.link.counts().resends);
    CHECK(r.link.framed());                    // without giving the link up
    REQUIRE(r.rom.applied.size() == sent.size());  // none lost, none applied twice
    CHECK(r.rom.applied == sent);                  // and in order
}

TEST_CASE("a console RESET on the box's framed link: nothing but the guard's probes after the stall, and the menu's "
          "answer drops the ring and the window",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    const Bytes a = note(0x90, 60, 100), b = note(0x90, 62, 100), c = note(0x90, 64, 100);
    REQUIRE(r.push(a));
    r.drain();
    REQUIRE(r.dev.applied.size() == 1);
    // The RESET: the menu, which acknowledges nothing, and whose booting OS answers '*t' with nothing useful at first.
    r.dev.testReply = 0x00;
    r.dev.resetToMenu();
    REQUIRE(r.push(b));
    r.drain();  // b's frame, which the menu reads and skips (no 0x2A in it)
    const std::size_t atStall = fifoWrites(r.dev).size();
    r.drain(100);  // 100 ms: the window has stopped moving: held, and the menu asked
    CHECK(r.link.connected());
    REQUIRE(r.push(c));  // held in the ring, not written
    r.drain(500);
    r.dev.testReply = 'k';  // the OS is up
    r.drain(500);           // a second on: asked again
    r.drain();              // its 'k'
    CHECK(r.link.atMenu());
    CHECK_FALSE(r.link.connected());
    const std::vector<std::string> writes = fifoWrites(r.dev);
    CHECK(std::vector<std::string>(writes.begin() + static_cast<std::ptrdiff_t>(atStall), writes.end()) ==
          std::vector<std::string>{fifoEntry(kProbeBytes), fifoEntry(kProbeBytes)});
    CHECK(r.link.counts().notReady == 2);  // c from the ring, b from the window
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK_FALSE(r.push(note(0x80, 60, 0)));  // at the menu: refused, counted
    CHECK(r.link.counts().notReady == 3);
    CHECK(r.dev.applied.size() == 1);
    CHECK_FALSE(r.dev.error40);
    CHECK(r.dev.menu);
}

TEST_CASE("a console RESET on the box's raw link is seen by the guard's probe, every 250 ms between writes",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.link.setConnected(true);
    r.drain();
    r.drain(100);  // raw: the first probe, which the game ignores
    REQUIRE_FALSE(r.link.framed());
    std::uint64_t resetUs = 0;
    for (int i = 0; i < 20 && !r.link.atMenu(); ++i) {
        if (i == 2) {
            r.dev.resetToMenu();
            resetUs = r.us;
        }
        REQUIRE(r.push(note(0x90, static_cast<std::uint8_t>(60 + i), 100)));
        r.drain(50);
    }
    CHECK(r.link.atMenu());
    CHECK(r.us - resetUs <= static_cast<std::uint64_t>(rp::N8MenuGuard::kProbeNs / 1000) + 50'000);
    CHECK(r.dev.countExact(fifoEntry(kProbeBytes)) == 1);  // the first; the answered one rode with a message
    CHECK_FALSE(r.dev.error40);
    CHECK_FALSE(r.push(note(0x80, 60, 0)));
}

TEST_CASE("two stall probes the box's framed link hears nothing to are a game without the link: the link is probed "
          "again, and streams raw",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    // The game stops answering, as one without the link does when a reset the guard never saw started it.
    r.dev.linkDeaf = true;
    REQUIRE(r.push(note(0x90, 60, 100)));
    r.drain();      // its frame, which nothing acknowledges
    r.drain(100);   // the stall: held, probed
    r.drain(1000);  // nothing heard: probed again
    REQUIRE(r.push(note(0x90, 62, 100)));  // held
    r.drain(1000);  // nothing again: the window and the ring go, and the link is probed again
    CHECK(r.link.counts().notReady == 2);
    r.drain();     // the sync, which nothing answers
    r.drain(100);  // raw, and the guard's first raw probe
    CHECK_FALSE(r.link.framed());
    const Bytes after = note(0x90, 64, 100);
    REQUIRE(r.push(after));
    r.drain();
    const std::vector<std::string> writes = fifoWrites(r.dev);
    REQUIRE(writes.size() >= 3);
    CHECK(std::vector<std::string>(writes.end() - 3, writes.end()) ==
          std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(kProbeBytes), fifoEntry(after)});
    CHECK(r.dev.countExact(fifoEntry({0xF4, 0x00})) == 2);  // the connect's probe and the second
    CHECK(r.dev.countExact(fifoEntry(kProbeBytes)) == 3);   // two stall probes and the raw link's first
    CHECK(r.link.counts().resends == 0);                    // a held stall re-sends nothing
    CHECK(r.link.counts().resent == 0);
}

TEST_CASE("the box's link is quiet for an Edio read only when nothing it is owed can land in the reply",
          "[device-mode][box][box-link]") {
    ScriptedRig r;
    SECTION("not while the connect's sync waits for its ack, nor while a guard probe awaits its answer") {
        r.link.setConnected(true);
        CHECK(r.link.quietForRead());  // nothing written yet
        r.drain();                     // the sync
        CHECK_FALSE(r.link.quietForRead());
        r.drain(100);                  // raw, and the guard's probe
        REQUIRE_FALSE(r.link.framed());
        REQUIRE(r.writes.back() == kProbeBytes);
        CHECK_FALSE(r.link.quietForRead());
        r.us += 99'000;
        CHECK_FALSE(r.link.quietForRead());
        r.us += 1'000;  // 100 ms: nothing answered, a game
        CHECK(r.link.quietForRead());
    }
    SECTION("not inside an ack") {
        r.connectFramed();
        REQUIRE(r.link.framed());
        CHECK(r.link.quietForRead());
        r.back = {0xF5, 0x00};
        r.drain();
        CHECK_FALSE(r.link.quietForRead());
        r.back = {0x7F};
        r.drain();
        CHECK(r.link.quietForRead());
    }
    SECTION("not with a fresh frame outstanding, until the cart has said nothing for 250 ms since it went") {
        r.connectFramed();
        REQUIRE(r.push(note(0x90, 60, 100)));
        r.drain();
        REQUIRE(r.link.outstanding() == 1);
        CHECK_FALSE(r.link.quietForRead());
        r.us += 249'000;
        CHECK_FALSE(r.link.quietForRead());
        r.us += 1'000;
        CHECK(r.link.quietForRead());
        r.back = {0xF5, 0x01, 0x00};
        r.drain();
        CHECK(r.link.outstanding() == 0);
        CHECK(r.link.quietForRead());
    }
}

TEST_CASE("a SysEx longer than a frame carries is dropped on the box's framed link, and counted",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    Bytes big(45, 0x10);
    big.front() = 0xF0;
    big.back() = 0xF7;
    const Bytes on = note(0x90, 60, 100);
    REQUIRE(r.push(big));
    REQUIRE(r.push(on));
    r.drain();
    CHECK(r.link.counts().unframed == 1);
    CHECK(r.link.counts().forwarded == 1);
    CHECK(r.link.counts().sysex == 0);
    CHECK(r.dev.applied == std::vector<Bytes>{on});
}

TEST_CASE("the box's pass on the framed link: the connect's probe, a frame and its ack, allocating nothing",
          "[device-mode][box][box-link]") {
    auto box = std::make_unique<Box>();
    auto record = std::make_unique<BoxRecord>();
    record->clockCtx = box.get();
    record->now = &boxNow;
    record->arrival = &boxArrival;
    record->monotonicUs = &boxNow64;
    record->backChannel = true;
    void* link = rp_devtest_box_link_device_mode(record.get());
    box->load(link, &rp_devtest_box_acquired_device_mode, &rp_devtest_box_released_device_mode);
    auto drain = [&] { rp_devtest_box_drain_device_mode(static_cast<std::uint32_t>(box->clock.us / 1000)); };
    // A note for the game, on the UART's ring before the window (a ring that grows allocates), held back from the
    // passes until the link is framed.
    box->ringEnd = 0;
    box->arrive({0x90, 0x45, 0x7F});
    const std::uint32_t noteAt = static_cast<std::uint32_t>(box->clock.us);

    // The plan's first block prints its package listing (a string, once), so it runs before the count, with nothing
    // for the game yet (the note is held back and no game runs).
    REQUIRE(box->pass(1'000));
    rp::test::beginCountingAllocations();
    rp_devtest_box_connect_device_mode(true);
    const bool ranOne = box->pass(1'000);
    drain();                             // the connect's probe: F4 00
    say(*record, {0xF5, 0x00, 0x7F});    // BlipToaster's ack for it
    const bool ranTwo = box->pass(500);
    drain();                             // framed, and the volume at the connect
    box->ringEnd = SIZE_MAX;
    const bool ranThree = box->pass(500);
    drain();                             // the note, framed behind the sender's own sync
    say(*record, {0xF5, 0x01, 0x00});    // its ack
    const bool ranFour = box->pass(500);
    drain();
    const auto counted = rp::test::endCountingAllocations();

    CHECK(ranOne);
    CHECK(ranTwo);
    CHECK(ranThree);
    CHECK(ranFour);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    const BoxCounts c = boxCounts();
    CHECK(c.framed);
    CHECK(writeLines(*record) == "0: f4 0\n1: f4 0 f5 0 90 45 7f 54\n");
    CHECK(record->writes[1].firstUs == noteAt);
    CHECK(record->volumeCount == 1);
    CHECK(record->volumes[0] == 128);
    CHECK(c.forwarded == 1);
    CHECK(c.resends == 0);
    CHECK(c.unframed == 0);
    CHECK(record->backCount == 0);
    unloadPlan(box->plan.plan());
}

TEST_CASE("a framed link the box disconnects and connects again is probed afresh, with a sender and a guard of its own",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    REQUIRE(r.push(note(0x90, 60, 100)));
    r.drain();
    REQUIRE(r.link.outstanding() == 1);
    REQUIRE(r.dev.syncs == 2);  // the connect's probe and the sender's own
    r.link.setConnected(false);
    CHECK_FALSE(r.link.framed());
    CHECK(r.link.outstanding() == 0);
    CHECK(r.link.counts().notReady == 1);  // the window's unacknowledged frame
    CHECK_FALSE(r.link.atMenu());
    r.link.setConnected(true);
    r.drain();
    CHECK(r.link.framed());
    CHECK(r.dev.countExact(fifoEntry({0xF4, 0x00})) == 2);  // the connect's probe, again
    REQUIRE(r.push(note(0x90, 64, 100)));
    r.drain();
    CHECK(r.dev.applied.size() == 2);
    CHECK(r.link.counts().notReady == 1);
}

TEST_CASE("the box's raw link probes no sooner than 250 ms after its last probe", "[device-mode][box][box-link]") {
    ScriptedRig r;
    r.link.setConnected(true);
    r.drain();
    r.drain(100);  // raw, and the guard's first probe
    REQUIRE_FALSE(r.link.framed());
    std::vector<std::uint64_t> probeAt;
    std::size_t                seen = r.writes.size();
    probeAt.push_back(r.us);  // the first probe went out in the drain above
    for (int i = 0; i < 100; ++i) {
        r.drain(10);
        for (; seen < r.writes.size(); ++seen)
            if (r.writes[seen] == kProbeBytes) probeAt.push_back(r.us);
    }
    REQUIRE(probeAt.size() >= 3);
    for (std::size_t i = 1; i < probeAt.size(); ++i) CHECK(probeAt[i] - probeAt[i - 1] >= 250'000);
}

TEST_CASE("a failed framed write on the box's link: its frame is marked sent, re-sent once the cart shows it lost, and "
          "every message arrives in order",
          "[device-mode][box][box-link]") {
    LossyRig r;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    const Bytes a = note(0x90, 60, 100), b = note(0x90, 62, 100), c = note(0x90, 64, 100);
    REQUIRE(r.push(a));
    r.failNext = true;
    r.drain();
    CHECK(r.link.counts().failed == 1);
    // Written as far as the link is concerned, whether or not the write went through: a frame left unmarked would
    // never be re-sent, and the oldest-sent time the quiet rule reads would never come, wedging the stream behind it.
    CHECK(r.link.outstanding() == 1);
    CHECK(r.rom.applied.empty());
    REQUIRE(r.push(b));
    r.drain();  // b's frame, which the cart reads past a gap and answers with the frame it still wants
    REQUIRE(r.push(c));
    for (int k = 0; k < 400 && (r.link.queued() != 0 || r.link.outstanding() != 0); ++k) r.drain();
    CHECK(r.link.outstanding() == 0);
    CHECK(r.link.counts().resends >= 1);
    CHECK(r.link.counts().resent >= 1);
    CHECK(r.rom.applied == std::vector<Bytes>{a, b, c});
    CHECK(r.link.quietForRead());
}
