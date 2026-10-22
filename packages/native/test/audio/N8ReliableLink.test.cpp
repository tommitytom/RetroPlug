// Guards BlipToaster's reliable link - the host half (transport/N8ReliableLink.hpp) and the emulated cart FIFO's use
// of it (NesN8FifoRole). The ROM half is pinned in the BlipToaster repo (retroplug-cli/tests/link.test.ts); here a
// small model of its rules (main.c "The reliable link") stands in for it, so the sender can be driven through a
// LOSSY channel shaped like the real one: on NTSC a DMC-DMA halt on `lda $40F0` repeats the read and the N8 FIFO
// pops a byte per read, so 1-3 consecutive bytes vanish. The property that matters is exact: whatever is lost, the
// ROM ends up applying every message once, in order.
//
// Run via `pnpm test:plugin` (retroplug-audio-test).

#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/mesen/roles/NesN8FifoRole.hpp"
#include "transport/N8ReliableLink.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;
using rp::N8ReliableSender;

// The ROM's link rules (bliptoaster src/midi/main.c), as a byte-at-a-time parser. `applied` collects the messages it
// would act on; ack() reports what it would write on the back-channel when its FIFO runs empty.
struct RomModel {
    bool         linkOn = false;
    std::uint8_t next   = 0;
    bool         dirty  = false;
    enum { Idle, WantSync, WantSeq, InMsg, WantSum } st = Idle;
    std::uint8_t seq = 0;
    unsigned     sum = 0;
    std::size_t  need = 0;   // message length incl. status; 0 = a SysEx (to F7)
    Bytes        buf;
    std::vector<Bytes> applied;

    void bad() { st = Idle; dirty = true; }

    void byte(std::uint8_t b) {
        if (b >= 0xF8) return;                                          // real-time: skipped (0xFF panic not modelled)
        switch (st) {
            case WantSync:
                if (b & 0x80) { st = Idle; byte(b); return; }
                linkOn = true; next = b; dirty = true; st = Idle; return;
            case WantSeq:
                if (b & 0x80) { bad(); byte(b); return; }               // header cut short
                seq = b; sum = b; buf.clear(); st = InMsg; return;
            case InMsg:
                if (buf.empty()) {
                    if (!(b & 0x80)) { bad(); return; }                 // the status was lost
                    if (b == 0xF4 || b == 0xF5) { bad(); byte(b); return; }
                    need = (b == 0xF0) ? 0 : (((b & 0xE0) == 0xC0) ? 2 : 3);
                } else if ((b & 0x80) && !(need == 0 && b == 0xF7)) {
                    bad(); byte(b); return;                             // a status where data was due: cut
                }
                if (buf.size() == 40) { bad(); return; }
                buf.push_back(b); sum += b;
                if ((need && buf.size() == need) || b == 0xF7) st = WantSum;
                return;
            case WantSum:
                if (b & 0x80) { bad(); byte(b); return; }
                st = Idle;
                if (b != (sum & 0x7F)) { dirty = true; return; }
                if (!linkOn) { linkOn = true; next = seq; }
                if (seq != next) { dirty = true; return; }
                applied.push_back(buf);
                next = static_cast<std::uint8_t>((next + 1) & 0x7F);
                dirty = true;
                return;
            case Idle:
                if (b == 0xF4) st = WantSync;
                else if (b == 0xF5) st = WantSeq;
                else if (linkOn) dirty = true;                          // unframed: dropped, and reported
                return;
        }
    }

    bool ack(Bytes& out) {
        if (!dirty) return false;
        dirty = false;
        out = { 0xF5, next };
        return true;
    }
};

Bytes noteOn(std::uint8_t ch, std::uint8_t n, std::uint8_t v) { return { std::uint8_t(0x90 | ch), n, v }; }

// A frame as the sender writes it: F5 seq message sum.
Bytes frame(std::uint8_t seq, const Bytes& msg) {
    Bytes f = { 0xF5, seq };
    unsigned sum = seq;
    for (auto b : msg) { f.push_back(b); sum += b; }
    f.push_back(static_cast<std::uint8_t>(sum & 0x7F));
    return f;
}
Bytes cat(std::initializer_list<Bytes> parts) { Bytes o; for (auto& p : parts) o.insert(o.end(), p.begin(), p.end()); return o; }

}  // namespace

TEST_CASE("reliable link: messages go out as frames behind one sync", "[audio][nes][link]") {
    N8ReliableSender s;
    Bytes wire;
    s.send(noteOn(0, 60, 100).data(), 3, [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    const Bytes cc = { 0xB1, 7, 90 };
    s.send(cc.data(), 3, [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0x90, 60, 100 }), frame(1, { 0xB1, 7, 90 }) }));
    CHECK(s.outstanding() == 2);
}

TEST_CASE("reliable link: running status is expanded, real-time passes through, SysEx is one frame", "[audio][nes][link]") {
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

TEST_CASE("reliable link: a message split across sends is still one frame", "[audio][nes][link]") {
    N8ReliableSender s;
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); };
    const Bytes a = { 0xF0, 0x7D, 0x42 }, b = { 0x02, 0xF7 };
    s.send(a.data(), a.size(), emit);
    CHECK(wire.empty());
    s.send(b.data(), b.size(), emit);
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0xF0, 0x7D, 0x42, 0x02, 0xF7 }) }));
}

TEST_CASE("reliable link: an ack frees the window; an ack naming a sent frame re-sends from it", "[audio][nes][link]") {
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

    const Bytes ack1 = { 0xF5, 1 };   // frame 0 arrived; 1 (and so 2) did not
    s.onBackChannel(ack1.data(), ack1.size(), 500);
    CHECK(s.outstanding() == 2);

    wire.clear();
    CHECK(s.poll(1000, emit) != N8ReliableSender::kNever);   // not yet: the modelled round trip is 1000 ns
    CHECK(wire.empty());
    s.poll(1500, emit);
    CHECK(wire == cat({ frame(1, { 0x90, 62, 100 }), frame(2, { 0x90, 64, 100 }) }));
    CHECK(s.resends() == 1);

    const Bytes ack3 = { 0xF5, 3 };
    s.onBackChannel(ack3.data(), ack3.size(), 2000);
    CHECK(s.outstanding() == 0);
}

TEST_CASE("reliable link: a frame nothing answers is re-sent on the timeout", "[audio][nes][link]") {
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

TEST_CASE("reliable link: a ROM that lost track (rebooted) is re-synced to the oldest frame", "[audio][nes][link]") {
    N8ReliableSender s;
    Bytes wire;
    auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        wire.insert(wire.end(), d, d + n);
        if (seq >= 0) s.onSent(seq, 0);
    };
    for (std::uint8_t n : { 60, 62 }) s.send(noteOn(0, n, 100).data(), 3, emit);
    const Bytes ack = { 0xF5, 90 };   // a number we never sent
    s.onBackChannel(ack.data(), ack.size(), 10);
    wire.clear();
    s.poll(10, emit);
    CHECK(wire == cat({ { 0xF4, 0 }, frame(0, { 0x90, 60, 100 }), frame(1, { 0x90, 62, 100 }) }));
}

TEST_CASE("reliable link: through a channel that eats 1-3 byte bursts, every message applies once, in order",
          "[audio][nes][link]") {
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

TEST_CASE("reliable link: the emulated FIFO role frames MIDI and consumes the ROM's acks", "[audio][nes][link]") {
    NesN8FifoRole role;
    std::uint64_t cycles = 0;
    role.setClock([&cycles] { return cycles; }, 1'789'773.0);
    N8ReliableSender::Timing t;
    t.resendDelayNs = 0;
    role.enableLink(t);

    ::MidiEvent e{};
    e.frame = 0; e.size = 3; e.data[0] = 0x90; e.data[1] = 60; e.data[2] = 100;
    role.onMidi(&e, 1);
    role.pumpUntil(0);
    Bytes got;
    for (int b; (b = role.readFifoByte()) >= 0;) got.push_back(static_cast<std::uint8_t>(b));
    CHECK(got == cat({ { 0xF4, 0 }, frame(0, { 0x90, 60, 100 }) }));
    CHECK(role.link()->outstanding() == 1);

    // The ROM's ack, written as the cart does it: an Edio CMD_USB_WR with payload F5 <next>. The role takes it
    // (drainUsbTx stays empty) and the window frees.
    const Bytes cmd = { '+', std::uint8_t('+' ^ 0xFF), 0x22, std::uint8_t(0x22 ^ 0xFF), 2, 0, 0xF5, 1 };
    // Writes go through the FIFO's register, as the ROM's STA $40F0 would.
    for (auto b : cmd) role.writeFifoByte(b);
    CHECK(role.drainUsbTx().empty());
    CHECK(role.link()->outstanding() == 0);

    // A lost frame: the ack says 1 is still expected after we sent 1 - the role re-sends it on the next pump.
    e.data[1] = 62;
    role.onMidi(&e, 1);
    role.pumpUntil(0);
    while (role.readFifoByte() >= 0) {}            // the "ROM" read it... and lost it
    cycles += 1000;
    for (auto b : cmd) role.writeFifoByte(b);      // still F5 1
    role.pumpUntil(0);
    got.clear();
    for (int b; (b = role.readFifoByte()) >= 0;) got.push_back(static_cast<std::uint8_t>(b));
    CHECK(got == frame(1, { 0x90, 62, 100 }));
}

TEST_CASE("reliable link: a SysEx longer than the ROM's frame buffer is not framed", "[audio][nes][link]") {
    N8ReliableSender s;
    Bytes wire;
    Bytes big = { 0xF0 };
    for (int i = 0; i < 45; ++i) big.push_back(0x10);
    big.push_back(0xF7);
    s.send(big.data(), big.size(), [&](const std::uint8_t* d, std::size_t n, int) { wire.insert(wire.end(), d, d + n); });
    CHECK(wire.empty());
    CHECK(s.outstanding() == 0);
}

TEST_CASE("reliable link (hardware timing): an ack too early to blame a frame re-checks it once it is old enough",
          "[audio][nes][link]") {
    // On a real N8 an ack can be written while a younger frame is still crossing USB, so a frame is only blamed once
    // it is older than the fastest round trip. An early ack must not simply be ignored - that left a loss for the
    // NEXT message's ack to report, half a step late - it schedules a re-check; an ack showing arrival cancels it.
    N8ReliableSender::Timing t;
    t.adaptiveCondemn = true;
    t.rttGuessNs      = 3'000'000;
    t.condemnAfterNs  = 500'000;
    t.timeoutNs       = 1'000'000'000;
    Bytes wire;
    std::int64_t now = 0;

    SECTION("lost: re-sent once it is old enough") {
        N8ReliableSender s(t);
        auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) { wire.insert(wire.end(), d, d + n); if (seq >= 0) s.onSent(seq, now); };
        s.send(noteOn(0, 60, 100).data(), 3, emit);
        const Bytes early = { 0xF5, 0 };          // at 1 ms: frame 0 is too young to blame
        now = 1'000'000; s.onBackChannel(early.data(), early.size(), now);
        wire.clear();
        now = 3'000'000; s.poll(now, emit);
        CHECK(wire.empty());                      // not ripe until 3.5 ms
        now = 3'600'000; s.poll(now, emit);
        CHECK(wire == frame(0, { 0x90, 60, 100 }));
    }
    SECTION("arrived: a later ack cancels the re-check") {
        N8ReliableSender s(t);
        auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) { wire.insert(wire.end(), d, d + n); if (seq >= 0) s.onSent(seq, now); };
        s.send(noteOn(0, 60, 100).data(), 3, emit);
        const Bytes early = { 0xF5, 0 }, later = { 0xF5, 1 };
        now = 1'000'000; s.onBackChannel(early.data(), early.size(), now);
        now = 2'500'000; s.onBackChannel(later.data(), later.size(), now);
        wire.clear();
        now = 10'000'000; s.poll(now, emit);
        CHECK(wire.empty());
        CHECK(s.outstanding() == 0);
    }
}
