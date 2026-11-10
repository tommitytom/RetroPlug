// Guards the sender half of BlipToaster's reliable link (transport/N8ReliableLink.hpp, the n8-wire package's): messages
// framed behind one sync, the window and its acks, running status and SysEx, a re-send on the timeout, a ROM that lost
// track, and a soak through a channel that eats 1-3 byte bursts, where every message must apply once, in order. The
// emulated cart FIFO that speaks the link from the other side is the Mesen package's, and its own cases are that package's
// test binary's (packages/mesen/native/test/N8ReliableLink.test.cpp).
//
// Run via `pnpm test:n8` (retroplug-n8-test).

#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "LinkRomModel.hpp"
#include "transport/N8ReliableLink.hpp"

namespace {

using rp::n8::test::Bytes;
using rp::n8::test::cat;
using rp::n8::test::frame;
using rp::n8::test::noteOn;
using rp::n8::test::RomModel;
using rp::N8ReliableSender;

}  // namespace

TEST_CASE("reliable link: messages go out as frames behind one sync", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    s.send(noteOn(0, 60, 100).data(), 3,
           [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    const Bytes cc = { 0xB1, 7, 90 };
    s.send(cc.data(), 3, [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0x90, 60, 100 }), frame(1, { 0xB1, 7, 90 }) }));
    CHECK(s.outstanding() == 2);
}

TEST_CASE("reliable link: a window that fills without an ack gives up the window and re-syncs, not grows",
          "[n8][link]") {
    // The sender's window is 48 frames. A cart that stops answering must not make it grow: at the 49th frame the
    // whole window is given up and a sync goes out ahead of the new frame, so the ROM is re-pointed at what follows.
    N8ReliableSender s;
    Bytes wire;
    int framesSentByEmit = 0;  // the seq >= 0 marker counts first sends (and later re-sends) on the wire
    const auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) ++framesSentByEmit;
    };
    for (int i = 0; i < 49; ++i) {
        const Bytes msg = noteOn(0, static_cast<std::uint8_t>(60 + i % 12), 100);
        s.send(msg.data(), msg.size(), emit);
    }
    CHECK(s.framesSent() == 49);
    CHECK(framesSentByEmit == 49);
    CHECK(s.outstanding() == 1);   // the 49th send cleared the window before its own frame
    // And the re-sync it gave the ROM first: F4 <next seq> immediately ahead of that 49th frame, so the ROM is
    // re-pointed at what follows rather than left expecting a frame that is never coming.
    const Bytes last = frame(48, noteOn(0, 60, 100));
    REQUIRE(wire.size() >= last.size() + 2);
    CHECK(Bytes(wire.end() - static_cast<std::ptrdiff_t>(last.size()), wire.end()) == last);
    CHECK(wire[wire.size() - last.size() - 2] == 0xF4);
    CHECK(wire[wire.size() - last.size() - 1] == 48);
}

TEST_CASE("reliable link: running status is expanded, real-time passes through, SysEx is one frame", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); };
    const Bytes in = { 0x90, 60, 100, 64, 100, 0xF8, 0xF0, 0x7D, 0x42, 0x02, 0xF7, 0xC3, 5 };
    s.send(in.data(), in.size(), emit);
    CHECK(wire == cat({ { 0xF4, 0 },
                        frame(0, { 0x90, 60, 100 }),
                        frame(1, { 0x90, 64, 100 }),
                        { 0xF8 },
                        frame(2, { 0xF0, 0x7D, 0x42, 0x02, 0xF7 }),
                        frame(3, { 0xC3, 5 }) }));
}

TEST_CASE("reliable link: a message split across sends is still one frame", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); };
    const Bytes a = { 0xF0, 0x7D, 0x42 }, b = { 0x02, 0xF7 };
    s.send(a.data(), a.size(), emit);
    CHECK(wire.empty());
    s.send(b.data(), b.size(), emit);
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0xF0, 0x7D, 0x42, 0x02, 0xF7 }) }));
}

TEST_CASE("reliable link: an ack frees the window; an ack naming a sent frame re-sends from it", "[n8][link]") {
    N8ReliableSender::Timing t;
    t.resendDelayNs = 1000;
    N8ReliableSender s(t);
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, 0);
    };
    for (std::uint8_t n : { 60, 62, 64 }) s.send(noteOn(0, n, 100).data(), 3, emit);
    REQUIRE(s.outstanding() == 3);

    const Bytes ack1 = { 0xF5, 1, 2 };   // frame 0 arrived; 1 did not - the ROM read 2 (and dropped it behind the gap)
    s.onBackChannel(ack1.data(), ack1.size(), 500);
    CHECK(s.outstanding() == 2);

    wire.clear();
    CHECK(s.poll(1000, emit) != N8ReliableSender::kNever);   // not yet: the modelled round trip is 1000 ns
    CHECK(wire.empty());
    s.poll(1500, emit);
    CHECK(wire == cat({ frame(1, { 0x90, 62, 100 }), frame(2, { 0x90, 64, 100 }) }));
    CHECK(s.resends() == 1);

    const Bytes ack3 = { 0xF5, 3, 2 };
    s.onBackChannel(ack3.data(), ack3.size(), 2000);
    CHECK(s.outstanding() == 0);
}

TEST_CASE("reliable link: a frame nothing answers is re-sent on the timeout", "[n8][link]") {
    N8ReliableSender::Timing t;
    t.timeoutNs = 10'000;
    N8ReliableSender s(t);
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, 0);
    };
    s.send(noteOn(0, 60, 100).data(), 3, emit);
    wire.clear();
    s.poll(9'999, emit);
    CHECK(wire.empty());
    s.poll(10'000, emit);
    CHECK(wire == frame(0, { 0x90, 60, 100 }));
}

TEST_CASE("reliable link: a ROM that lost track (rebooted) is re-synced to the oldest frame", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, 0);
    };
    for (std::uint8_t n : { 60, 62 }) s.send(noteOn(0, n, 100).data(), 3, emit);
    const Bytes ack = { 0xF5, 90, 89 };   // a number we never sent
    s.onBackChannel(ack.data(), ack.size(), 10);
    wire.clear();
    s.poll(10, emit);
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0x90, 60, 100 }), frame(1, { 0x90, 62, 100 }) }));
}

TEST_CASE("reliable link: through a channel that eats 1-3 byte bursts, every message applies once, in order",
          "[n8][link]") {
    // A seeded, deterministic soak: 2000 messages (notes, CCs, the odd SysEx) in bursts, each byte on the wire
    // at ~1.5% risk of starting a burst of 1-3 losses - an order of magnitude worse than a DMC beat on NTSC.
    std::mt19937 rng(1234);
    N8ReliableSender::Timing t;
    t.resendDelayNs = 2'000'000;
    t.timeoutNs     = 20'000'000;
    N8ReliableSender s(t);
    RomModel rom;

    std::int64_t now = 0;
    Bytes inFlight;                                   // what the "FIFO" will deliver at the next delivery tick
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        inFlight.insert(inFlight.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, now);
    };
    auto deliver = [&] {
        std::uniform_int_distribution<int> pct(0, 999), burst(1, 3);
        for (std::size_t i = 0; i < inFlight.size(); ++i) {
            if (pct(rng) < 15) { i += static_cast<std::size_t>(burst(rng)) - 1; continue; }   // lost
            rom.byte(inFlight[i]);
        }
        inFlight.clear();
        Bytes ack;
        if (rom.ack(ack)) s.onBackChannel(ack.data(), ack.size(), now);   // the FIFO ran empty
    };

    std::vector<Bytes> sent;
    for (int i = 0; i < 2000; ++i) {
        Bytes m;
        switch (i % 7) {
            case 3:  m = { 0xB1, std::uint8_t(i & 0x7F), std::uint8_t((i * 3) & 0x7F) }; break;
            case 6:  m = { 0xF0, 0x7D, 0x42, 0x02, 0xF7 }; break;
            default: m = noteOn(std::uint8_t(i % 4), std::uint8_t(36 + i % 40), 100); break;
        }
        sent.push_back(m);
        s.send(m.data(), m.size(), emit);
        if (i % 3 == 2) {                             // a sequencer step: deliver, then let time pass
            deliver();
            for (int k = 0; k < 4; ++k) { now += 1'000'000; s.poll(now, emit); deliver(); }
        }
    }
    for (int k = 0; k < 2000 && s.outstanding(); ++k) { now += 1'000'000; s.poll(now, emit); deliver(); }

    CHECK(s.outstanding() == 0);
    CHECK(s.resends() > 0);                           // the channel really did lose frames
    REQUIRE(rom.applied.size() == sent.size());       // none lost, none applied twice
    std::size_t firstDiff = 0;
    while (firstDiff < sent.size() && rom.applied[firstDiff] == sent[firstDiff]) ++firstDiff;
    if (firstDiff < sent.size()) {
        std::string a, b;
        for (auto x : rom.applied[firstDiff]) a += std::to_string(x) + " ";
        for (auto x : sent[firstDiff]) b += std::to_string(x) + " ";
        INFO("first difference at message " << firstDiff << ": applied [" << a << "] sent [" << b << "]");
        CHECK(false);
    }
    CHECK(rom.applied == sent);                       // and in order
}


TEST_CASE("reliable link: a SysEx longer than the ROM's frame buffer is not framed", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    Bytes big = { 0xF0 };
    for (int i = 0; i < 45; ++i) big.push_back(0x10);
    big.push_back(0xF7);
    s.send(big.data(), big.size(),
           [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    CHECK(wire.empty());
    CHECK(s.outstanding() == 0);
}

TEST_CASE("reliable link: an ack that has not seen the frame waits; one that has re-sends it at once", "[n8][link]") {
    // <last> before <next>: the ROM has not read the frame - it is still on its way (on a cart, crossing USB; in the
    // emulator, landing while the ack is written). Re-sending on a timer then duplicated frames that were fine. <last>
    // at or past <next>: the ROM read it (or bytes it had to discard, which it reports as that frame): lost - re-send.
    N8ReliableSender s;
    Bytes wire;
    std::int64_t now = 0;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, now);
    };
    s.send(noteOn(0, 60, 100).data(), 3, emit);

    const Bytes notSeen = { 0xF5, 0, 0x7F };
    now = 1'000'000; s.onBackChannel(notSeen.data(), notSeen.size(), now);
    wire.clear();
    now = 50'000'000; s.poll(now, emit);
    CHECK(wire.empty());                         // in flight: nothing to do (well inside the 100 ms timeout)

    const Bytes seen = { 0xF5, 0, 0 };
    s.onBackChannel(seen.data(), seen.size(), now);
    s.poll(now, emit);
    CHECK(wire == frame(0, { 0x90, 60, 100 }));
}

TEST_CASE("reliable link: an ack after a frame arrived settles it - no re-send", "[n8][link]") {
    N8ReliableSender s;
    Bytes wire;
    std::int64_t now = 0;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, now);
    };
    s.send(noteOn(0, 60, 100).data(), 3, emit);
    const Bytes early = { 0xF5, 0, 0x7F }, later = { 0xF5, 1, 0 };
    now = 1'000; s.onBackChannel(early.data(), early.size(), now);
    now = 2'000'000; s.onBackChannel(later.data(), later.size(), now);
    wire.clear();
    now = 500'000'000; s.poll(now, emit);
    CHECK(wire.empty());
    CHECK(s.outstanding() == 0);
}

