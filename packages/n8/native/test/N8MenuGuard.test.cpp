// Guards the N8 menu guard's decisions (transport/N8MenuGuard.hpp, the n8-wire package's), a rule a case, driven by
// hand with a clock of the case's own: the raw link's cadence, only between two messages; a framed stall held and
// probed, and probed again each second; an ack's resume and its trusted second, which a drained window ends; two
// silent stalls asking for the link to be probed again; the menu's 'k' entering the menu, where nothing is written and
// the config block's start pattern leaves it; a 'k' inside an ack that is not the menu's; and the state a probe from
// outside the guard reports. The drivers' own cases are N8CartJobs.test.cpp's [menu-guard] (N8Link over FakeN8) and the
// device-mode binary's [box-link] (the Pico box's cart link).
//
// Run via `pnpm test:n8` (retroplug-n8-test).

#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"

namespace {

using rp::N8MenuGuard;
using rp::N8ReliableSender;

constexpr std::int64_t kMs = 1'000'000;
// The guard's clock is never 0: 0 is its "no probe awaited", as a steady clock never reads.
constexpr std::int64_t T0 = 1'000 * kMs;

// A framed stream as the guard sees it: the reliable link's sender, its frames marked sent when the case says.
struct Stream {
    N8ReliableSender s;
    void sendAt(std::int64_t at, std::uint8_t key = 60) {
        const std::uint8_t on[3] = {0x90, key, 100};
        s.send(on, 3, [&](const std::uint8_t*, std::size_t, int seq) {
            if (seq >= 0) s.onSent(seq, at);
        });
    }
    // The ROM's ack: every frame before `next` arrived.
    void ack(std::uint8_t next, std::int64_t at) {
        const std::uint8_t a[3] = {0xF5, next, 0};
        s.onBackChannel(a, 3, at);
    }
};

}  // namespace

TEST_CASE("menu guard: a raw link is probed every 250 ms, and only between two messages", "[n8][menu-guard]") {
    N8MenuGuard g;
    N8MenuGuard::Turn t = g.turn(T0, nullptr, true);
    CHECK(t.writeProbe);  // the first at once
    CHECK_FALSE(t.hold);
    CHECK(g.probeAwaited(T0 + 99 * kMs));
    CHECK_FALSE(g.probeAwaited(T0 + 100 * kMs));
    CHECK_FALSE(g.turn(T0 + 100 * kMs, nullptr, true).writeProbe);   // unanswered: a game; the next is 250 ms on
    CHECK_FALSE(g.turn(T0 + 249 * kMs, nullptr, true).writeProbe);
    CHECK_FALSE(g.turn(T0 + 250 * kMs, nullptr, false).writeProbe);  // inside a message: it waits, however long
    CHECK_FALSE(g.turn(T0 + 900 * kMs, nullptr, false).writeProbe);
    t = g.turn(T0 + 901 * kMs, nullptr, true);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: a framed window that stops moving is held and probed, and probed again each second",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    N8MenuGuard::Turn t = g.turn(T0 + 99 * kMs, &f.s, true);
    CHECK_FALSE(t.hold);  // inside the link's own timeout: the window may still move
    CHECK_FALSE(t.writeProbe);
    t = g.turn(T0 + 100 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK(g.probeAwaited(T0 + 150 * kMs));
    t = g.turn(T0 + 600 * kMs, &f.s, true);
    CHECK(t.hold);  // nothing heard: still held, and not asked again within the second
    CHECK_FALSE(t.writeProbe);
    t = g.turn(T0 + 1100 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.reprobeLink);
}

TEST_CASE("menu guard: the menu's 'k' heard while a framed window is stalled enters the menu, the hold ending in it",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    N8MenuGuard::Turn t = g.turn(T0 + 100 * kMs, &f.s, true);
    REQUIRE(t.hold);
    REQUIRE(t.writeProbe);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    t = g.turn(T0 + 120 * kMs, &f.s, true);
    CHECK(t.enteredMenu);
    CHECK(t.hold);
    CHECK_FALSE(t.reprobeLink);
    CHECK(g.atMenu());
    // The stall is forgotten: at the menu nothing is written, not a probe and not a re-send, and the stalled window is
    // no longer asked about; the config block is read instead.
    bool askedConfig = false;
    for (std::int64_t at = T0 + 121 * kMs; at < T0 + 1500 * kMs; at += 5 * kMs) {
        t = g.turn(at, &f.s, true);
        CHECK(t.hold);
        CHECK_FALSE(t.writeProbe);
        CHECK_FALSE(t.enteredMenu);
        CHECK_FALSE(t.reprobeLink);
        askedConfig = askedConfig || t.readConfig;
    }
    CHECK(askedConfig);
}

TEST_CASE("menu guard: an ack heard after a stall's probe resumes the stream, and a stall is not asked about again "
          "for a second",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    REQUIRE(g.turn(T0 + 100 * kMs, &f.s, true).writeProbe);
    const std::uint8_t probeAck[3] = {0xF5, 0x00, 0x00};  // a live BlipToaster's, for the probe's unframed bytes
    g.hear(probeAck, 3);
    N8MenuGuard::Turn t = g.turn(T0 + 110 * kMs, &f.s, true);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(t.writeProbe);
    SECTION("the trusted second: a window still stopped is not asked about again until it ends") {
        t = g.turn(T0 + 1109 * kMs, &f.s, true);
        CHECK_FALSE(t.hold);
        CHECK_FALSE(t.writeProbe);
        t = g.turn(T0 + 1110 * kMs, &f.s, true);
        CHECK(t.hold);
        CHECK(t.writeProbe);
    }
    SECTION("a window that drains ends it: the next frame's stall is asked about at the link's own timeout") {
        f.ack(1, T0 + 120 * kMs);
        REQUIRE(f.s.outstanding() == 0);
        CHECK_FALSE(g.turn(T0 + 120 * kMs, &f.s, true).hold);
        f.sendAt(T0 + 200 * kMs, 62);
        CHECK_FALSE(g.turn(T0 + 299 * kMs, &f.s, true).hold);
        t = g.turn(T0 + 300 * kMs, &f.s, true);
        CHECK(t.hold);
        CHECK(t.writeProbe);
    }
}

TEST_CASE("menu guard: two stall probes that hear nothing at all ask for the link to be probed again",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    REQUIRE(g.turn(T0 + 100 * kMs, &f.s, true).writeProbe);
    REQUIRE(g.turn(T0 + 1100 * kMs, &f.s, true).writeProbe);  // the first second heard nothing: asked again
    N8MenuGuard::Turn t = g.turn(T0 + 2099 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK_FALSE(t.reprobeLink);
    t = g.turn(T0 + 2100 * kMs, &f.s, true);
    CHECK(t.reprobeLink);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(t.writeProbe);
    CHECK_FALSE(g.probeAwaited(T0 + 2100 * kMs));
    CHECK_FALSE(g.atMenu());
}

namespace {

// A config block as the FPGA hands it over USB (16 bytes at 0x1800020): map_idx first.
using Block = std::array<std::uint8_t, N8MenuGuard::kConfigBytes>;
constexpr Block kPowerOnMenu = {0xFF, 0, 0, 0, 0, 0, 0, 0x80};
constexpr Block kZeros       = {};
constexpr Block kVrc6Game    = {0x18, 0x65, 0x04, 0x8B, 0, 0, 0, 0x9E, 0x14};
constexpr Block kFme7Game    = {0x45, 0x65, 0x02, 0x81, 0x01, 0, 0, 0x8E, 0x14};
// The guard at the menu is entered on this turn, and its first read is due once the answer window has passed.
constexpr std::int64_t kEntered   = T0 + 20 * kMs;
constexpr std::int64_t kFirstRead = kEntered + N8MenuGuard::kAnswerNs;

// A guard at the menu, entered by the menu's answer to its probe at T0.
N8MenuGuard atMenu() {
    N8MenuGuard g;
    REQUIRE(g.turn(T0, nullptr, true).writeProbe);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    REQUIRE(g.turn(kEntered, nullptr, true).enteredMenu);
    return g;
}

// The turn at `at`, the block handed back when it asked for one.
N8MenuGuard::Turn read(N8MenuGuard& g, std::int64_t at, const Block& block) {
    const N8MenuGuard::Turn t = g.turn(at, nullptr, true);
    if (t.readConfig) g.config(at, block.data());
    return t;
}

// Reads `block` on the cadence until `until`, `at` left on the last read; true while no turn left the menu.
bool stays(N8MenuGuard& g, std::int64_t& at, std::int64_t until, const Block& block) {
    bool stayed = true;
    while (at + N8MenuGuard::kConfigNs <= until) {
        at += N8MenuGuard::kConfigNs;
        stayed = !read(g, at, block).leftMenu && stayed;
    }
    return stayed;
}

}  // namespace

TEST_CASE("menu guard: at the menu nothing is written, and the config block is asked for every kConfigNs",
          "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    // The turn that entered waits out the answer window; from there, a read per kConfigNs and never a probe.
    std::int64_t lastRead = 0;
    int          reads    = 0;
    for (std::int64_t at = kEntered + 1 * kMs; at < T0 + 3000 * kMs; at += 1 * kMs) {
        const N8MenuGuard::Turn t = read(g, at, kPowerOnMenu);
        CHECK_FALSE(t.writeProbe);
        CHECK(t.hold);
        CHECK_FALSE(t.leftMenu);
        if (t.readConfig) {
            if (reads == 0) CHECK(at >= kEntered + N8MenuGuard::kAnswerNs);
            else CHECK(at - lastRead == N8MenuGuard::kConfigNs);
            lastRead = at;
            ++reads;
        }
    }
    CHECK(reads == (T0 + 3000 * kMs - 1 - kFirstRead) / N8MenuGuard::kConfigNs + 1);
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a different game's block leaves the menu once it has held for kSettleNs", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kPowerOnMenu);  // the reference
    at += N8MenuGuard::kConfigNs;
    read(g, at, kZeros);  // Start Game: the OS clears the block
    at += 5 * N8MenuGuard::kConfigNs;
    const std::int64_t settledFrom = at;
    N8MenuGuard::Turn t = read(g, at, kVrc6Game);  // the game's block
    CHECK_FALSE(t.leftMenu);
    CHECK(stays(g, at, settledFrom + N8MenuGuard::kSettleNs - N8MenuGuard::kConfigNs, kVrc6Game));  // still settling
    for (int i = 0; i < 3 && !t.leftMenu; ++i) {
        at += N8MenuGuard::kConfigNs;
        t = read(g, at, kVrc6Game);
    }
    CHECK(t.leftMenu);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: a different game's block with no clear read in between still leaves, but only once settled",
          "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kPowerOnMenu);
    at += N8MenuGuard::kConfigNs;
    const std::int64_t settledFrom = at;
    CHECK_FALSE(read(g, at, kVrc6Game).leftMenu);
    CHECK(stays(g, at, settledFrom + N8MenuGuard::kSettleNs - 1, kVrc6Game));
    // The read that completes the settle decides; the turn after it leaves.
    at = settledFrom + N8MenuGuard::kSettleNs;
    CHECK_FALSE(read(g, at, kVrc6Game).leftMenu);
    at += N8MenuGuard::kConfigNs;
    const N8MenuGuard::Turn t = read(g, at, kVrc6Game);
    CHECK(t.leftMenu);
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: the same game started again, through zeros to the identical block, leaves the menu",
          "[n8][menu-guard]") {
    // A RESET leaves the block at the game's config, and starting that game again ends on the very same bytes: only the
    // clear in between shows the start.
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kFme7Game);  // the reference: what the RESET left
    at += N8MenuGuard::kConfigNs;
    read(g, at, kZeros);
    at += N8MenuGuard::kConfigNs;
    const std::int64_t settledFrom = at;  // the first read of the game's block after the clear
    N8MenuGuard::Turn t = read(g, at, kFme7Game);
    for (int i = 0; i < 100 && !t.leftMenu; ++i) {
        at += N8MenuGuard::kConfigNs;
        t = read(g, at, kFme7Game);
    }
    CHECK(t.leftMenu);
    CHECK(at - settledFrom >= N8MenuGuard::kSettleNs);
}

TEST_CASE("menu guard: byte 3, the master volume the host writes, says nothing about a game start",
          "[n8][menu-guard]") {
    // The cart's expansion volume lives at byte 3 of the block, and the host writes it live, at the menu too: a block
    // that differs from the reference only there never leaves, however long it holds.
    {
        N8MenuGuard g = atMenu();
        std::int64_t at = kFirstRead;
        read(g, at, kFme7Game);  // the reference
        Block louder = kFme7Game;
        louder[3] = 0x80;
        at += N8MenuGuard::kConfigNs;
        CHECK(stays(g, at, at + 2000 * kMs, louder));
        louder[3] = 0xC8;
        CHECK(stays(g, at, at + 2000 * kMs, louder));
        CHECK(g.atMenu());
    }
    // A block that is all zero but for byte 3 is still the clear: a start whose clear left the volume alone.
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kFme7Game);
    Block clear = {};
    clear[3] = 0x80;
    at += N8MenuGuard::kConfigNs;
    read(g, at, clear);
    at += N8MenuGuard::kConfigNs;
    N8MenuGuard::Turn t = read(g, at, kFme7Game);
    for (int i = 0; i < 100 && !t.leftMenu; ++i) {
        at += N8MenuGuard::kConfigNs;
        t = read(g, at, kFme7Game);
    }
    CHECK(t.leftMenu);
}

TEST_CASE("menu guard: one garbled read followed by the unchanged block never leaves the menu", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kFme7Game);  // the reference
    Block garbled = kFme7Game;
    garbled[1]    = 0x6B;  // a stray back-channel byte in the reply
    at += N8MenuGuard::kConfigNs;
    CHECK_FALSE(read(g, at, garbled).leftMenu);
    CHECK(stays(g, at, at + 2000 * kMs, kFme7Game));
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a block that never changes, or never settles, keeps the guard at the menu", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    SECTION("unchanged") {
        read(g, at, kFme7Game);
        CHECK(stays(g, at, T0 + 5000 * kMs, kFme7Game));
    }
    SECTION("back to zeros inside the settle") {
        read(g, at, kPowerOnMenu);
        at += N8MenuGuard::kConfigNs;
        read(g, at, kZeros);
        for (int round = 0; round < 6; ++round) {  // just short of kSettleNs of the game's block, then zeros again
            CHECK(stays(g, at, at + N8MenuGuard::kSettleNs - 1, kVrc6Game));
            at += N8MenuGuard::kConfigNs;
            CHECK_FALSE(read(g, at, kZeros).leftMenu);
        }
    }
    SECTION("a block that keeps changing") {
        read(g, at, kPowerOnMenu);
        for (int i = 0; i < 100; ++i) {
            at += N8MenuGuard::kConfigNs;
            CHECK_FALSE(read(g, at, i % 2 ? kVrc6Game : kFme7Game).leftMenu);
        }
    }
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a read never handed back changes nothing", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kPowerOnMenu);
    at += N8MenuGuard::kConfigNs;
    read(g, at, kZeros);
    // Every read from here fails: the guard asks on its cadence and decides nothing.
    for (int i = 0; i < 100; ++i) {
        at += N8MenuGuard::kConfigNs;
        const N8MenuGuard::Turn t = g.turn(at, nullptr, true);
        CHECK(t.readConfig);
        CHECK_FALSE(t.leftMenu);
    }
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: entering the menu again starts from a fresh reference", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = kFirstRead;
    read(g, at, kPowerOnMenu);
    at += N8MenuGuard::kConfigNs;
    read(g, at, kZeros);  // a start begun...
    CHECK(g.report(false));  // ...and another probe said a game (a submission's), then the menu again
    CHECK(g.report(true));
    // The first block after the new entry is the reference: the game's block, steady, is neither different from it nor
    // preceded by a clear.
    for (int i = 0; i < 60; ++i) {
        at += N8MenuGuard::kConfigNs;
        CHECK_FALSE(read(g, at, kVrc6Game).leftMenu);
    }
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a 'k' inside an ack is not the menu's answer, even split across two reads", "[n8][menu-guard]") {
    N8MenuGuard        g;
    const std::uint8_t ack[3] = {0xF5, 0x6B, 0x6B};  // <next> and <last> both 0x6B
    g.hear(ack, 3);
    CHECK_FALSE(g.turn(T0, nullptr, true).enteredMenu);
    const std::uint8_t head[1] = {0xF5}, tail[2] = {0x6B, 0x6B};
    g.hear(head, 1);
    CHECK(g.scan().state == 1);
    g.hear(tail, 2);
    CHECK(g.scan().state == 0);
    CHECK_FALSE(g.turn(T0 + 1 * kMs, nullptr, true).enteredMenu);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    CHECK(g.turn(T0 + 2 * kMs, nullptr, true).enteredMenu);
}

TEST_CASE("menu guard: the state a probe from outside the guard reports, and the connect's seed", "[n8][menu-guard]") {
    N8MenuGuard g;
    g.seed(true);  // the connect's own check: the state, with nothing to enter
    CHECK(g.atMenu());
    CHECK_FALSE(g.report(true));  // already there: nothing changed
    CHECK(g.report(false));
    CHECK_FALSE(g.atMenu());
    CHECK(g.report(true));
    CHECK(g.atMenu());
    g.reset();
    CHECK_FALSE(g.atMenu());
    CHECK_FALSE(g.probeAwaited(T0));
}
