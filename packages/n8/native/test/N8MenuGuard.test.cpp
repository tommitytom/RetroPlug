// Guards the N8 menu guard's decisions (transport/N8MenuGuard.hpp, the n8-wire package's), a rule a case, driven by
// hand with a clock of the case's own: the raw link's cadence, only between two messages; a framed stall held and
// probed, and probed again each second; an ack's resume and its trusted second, which a drained window ends; two
// silent stalls asking for the link to be probed again; the menu's 'k' entering the menu and three misses in a row
// leaving it; a 'k' inside an ack that is not the menu's; and the state a probe from outside the guard reports. The
// drivers' own cases are N8CartJobs.test.cpp's [menu-guard] (N8Link over FakeN8) and the device-mode binary's
// [box-link] (the Pico box's cart link).
//
// Run via `pnpm test:n8` (retroplug-n8-test).

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

TEST_CASE("menu guard: the menu's 'k' enters the menu, and three unanswered probes in a row leave it",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    REQUIRE(g.turn(T0, nullptr, true).writeProbe);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    N8MenuGuard::Turn t = g.turn(T0 + 20 * kMs, nullptr, true);
    CHECK(t.enteredMenu);
    CHECK(t.hold);
    CHECK_FALSE(t.writeProbe);  // the cadence runs from the probe the menu answered
    CHECK(g.atMenu());
    t = g.turn(T0 + 250 * kMs, nullptr, true);  // at the menu, every 250 ms whatever the link
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.enteredMenu);
    t = g.turn(T0 + 350 * kMs, nullptr, true);  // unanswered: one miss
    CHECK(t.hold);
    CHECK_FALSE(t.leftMenu);
    REQUIRE(g.turn(T0 + 500 * kMs, nullptr, true).writeProbe);
    SECTION("three in a row: a game") {
        CHECK_FALSE(g.turn(T0 + 600 * kMs, nullptr, true).leftMenu);  // two
        REQUIRE(g.turn(T0 + 750 * kMs, nullptr, true).writeProbe);
        t = g.turn(T0 + 850 * kMs, nullptr, true);  // three
        CHECK(t.leftMenu);
        CHECK_FALSE(t.hold);
        CHECK_FALSE(t.writeProbe);
        CHECK_FALSE(g.atMenu());
    }
    SECTION("an answer between the misses starts the count again") {
        g.hear(&k, 1);
        t = g.turn(T0 + 520 * kMs, nullptr, true);
        CHECK(t.hold);
        CHECK_FALSE(t.enteredMenu);  // already there
        REQUIRE(g.turn(T0 + 750 * kMs, nullptr, true).writeProbe);
        CHECK_FALSE(g.turn(T0 + 850 * kMs, nullptr, true).leftMenu);   // one
        REQUIRE(g.turn(T0 + 1000 * kMs, nullptr, true).writeProbe);
        CHECK_FALSE(g.turn(T0 + 1100 * kMs, nullptr, true).leftMenu);  // two
        CHECK(g.atMenu());
    }
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
