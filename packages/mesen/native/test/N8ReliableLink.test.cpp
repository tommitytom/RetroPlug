// Guards the emulated cart FIFO's use of BlipToaster's reliable link (system/mesen/roles/NesN8FifoRole.cpp): the role
// frames MIDI and consumes the ROM's acks, numbers frames in the order it releases them, and carries a message into the
// next block so that it is numbered when it is released. The sender's own half of the link is the N8 pack's
// (packages/n8/native/test/N8ReliableLink.test.cpp), and the model of the ROM's rules both halves drive is the shared
// test helper library's LinkRomModel.hpp (@retroplug/testing): the pack may not include this package, and this one
// declares it.
//
// Run via `pnpm test:plugin` (retroplug-mesen-test).

#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "LinkRomModel.hpp"
#include "system/mesen/roles/NesN8FifoRole.hpp"
#include "transport/N8ReliableLink.hpp"

namespace {

using rp::n8::test::Bytes;
using rp::n8::test::cat;
using rp::n8::test::frame;
using rp::n8::test::RomModel;
using rp::N8ReliableSender;

}  // namespace

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
    const Bytes cmd = { '+', std::uint8_t('+' ^ 0xFF), 0x22, std::uint8_t(0x22 ^ 0xFF), 3, 0, 0xF5, 1, 0 };
    // Writes go through the FIFO's register, as the ROM's STA $40F0 would.
    for (auto b : cmd) role.writeFifoByte(b);
    CHECK(role.drainUsbTx().empty());
    CHECK(role.link()->outstanding() == 0);

    // A lost frame: the ack says 1 is still expected and that the ROM READ frame 1 (damaged) - re-sent on the next pump.
    e.data[1] = 62;
    role.onMidi(&e, 1);
    role.pumpUntil(0);
    while (role.readFifoByte() >= 0) {}            // the "ROM" read it... and lost it
    cycles += 1000;
    const Bytes damaged = { '+', std::uint8_t('+' ^ 0xFF), 0x22, std::uint8_t(0x22 ^ 0xFF), 3, 0, 0xF5, 1, 1 };
    for (auto b : damaged) role.writeFifoByte(b);  // F5 1 1: still expecting 1, having read it
    role.pumpUntil(0);
    got.clear();
    for (int b; (b = role.readFifoByte()) >= 0;) got.push_back(static_cast<std::uint8_t>(b));
    CHECK(got == frame(1, { 0x90, 62, 100 }));
}


TEST_CASE("reliable link: the emulated role numbers frames in the order it releases them", "[audio][nes][link]") {
    // A ROM already mid-stream (one frame applied) gets three messages queued out of offset order within one block:
    // onMidi at 100, a raw pushBytes SysEx at 20, onMidi at 50. pumpUntil releases by offset, so the sequence numbers
    // must follow that order - numbered in queue order instead, the ROM drops the early arrivals as out of sequence,
    // applies them late and out of order, and the sender has to re-send them.
    NesN8FifoRole role;
    std::uint64_t cycles = 0;
    role.setClock([&cycles] { return cycles; }, 1'789'773.0);
    N8ReliableSender::Timing t;
    t.resendDelayNs = 0;
    role.enableLink(t);

    // The ROM reads everything delivered so far, then answers on the back-channel as the cart does (CMD_USB_WR).
    RomModel rom;
    Bytes got;
    auto romReads = [&] {
        for (int v; (v = role.readFifoByte()) >= 0;) {
            got.push_back(static_cast<std::uint8_t>(v));
            rom.byte(static_cast<std::uint8_t>(v));
        }
        Bytes ack;
        if (!rom.ack(ack)) return;
        const Bytes cmd = { '+', std::uint8_t('+' ^ 0xFF), 0x22, std::uint8_t(0x22 ^ 0xFF),
                            std::uint8_t(ack.size()), 0 };
        for (auto v : cmd) role.writeFifoByte(v);
        for (auto v : ack) role.writeFifoByte(v);
    };

    ::MidiEvent prime{};
    prime.frame = 0; prime.size = 3; prime.data[0] = 0x92; prime.data[1] = 64; prime.data[2] = 80;
    role.onMidi(&prime, 1);
    role.pumpUntil(0);
    romReads();
    REQUIRE(rom.applied.size() == 1);
    REQUIRE(role.link()->outstanding() == 0);
    rom.applied.clear();
    got.clear();
    role.rebase(512);

    ::MidiEvent a{};
    a.frame = 100; a.size = 3; a.data[0] = 0x90; a.data[1] = 60; a.data[2] = 100;
    role.onMidi(&a, 1);
    const Bytes sysex = { 0xF0, 0x7D, 0x42, 0x02, 0xF7 };
    role.pushBytes(20, sysex.data(), sysex.size());
    ::MidiEvent b{};
    b.frame = 50; b.size = 3; b.data[0] = 0x91; b.data[1] = 62; b.data[2] = 90;
    role.onMidi(&b, 1);

    for (std::uint32_t off : { 20u, 50u, 100u }) {
        role.pumpUntil(off);
        romReads();
        cycles += 1000;
    }
    CHECK(got == cat({ frame(1, sysex), frame(2, { 0x91, 62, 90 }), frame(3, { 0x90, 60, 100 }) }));

    // Well past the sender's timeout: anything still unconfirmed would be re-sent here.
    cycles += 1'789'773 / 2;
    role.pumpUntil(512);
    romReads();

    REQUIRE(rom.applied.size() == 3);
    CHECK(rom.applied[0] == sysex);
    CHECK(rom.applied[1] == Bytes{ 0x91, 62, 90 });
    CHECK(rom.applied[2] == Bytes{ 0x90, 60, 100 });
    CHECK(role.link()->outstanding() == 0);
    CHECK(role.link()->resends() == 0);
}

TEST_CASE("reliable link: a message carried into the next block is numbered when it is released", "[audio][nes][link]") {
    // A message queued past the block end rides rebase into the next block. One queued there for an EARLIER offset
    // goes out first, so it must carry the earlier sequence number - framing at the carried message's first pump would
    // have numbered it first, and the ROM would drop it.
    NesN8FifoRole role;
    std::uint64_t cycles = 0;
    role.setClock([&cycles] { return cycles; }, 1'789'773.0);
    N8ReliableSender::Timing t;
    t.resendDelayNs = 0;
    role.enableLink(t);

    constexpr std::uint32_t kBlock = 512;
    ::MidiEvent late{};
    late.frame = kBlock + 100; late.size = 3; late.data[0] = 0x90; late.data[1] = 60; late.data[2] = 100;
    role.onMidi(&late, 1);
    role.pumpUntil(kBlock);
    CHECK(role.readFifoByte() < 0);   // not due this block
    role.rebase(kBlock);

    ::MidiEvent early{};
    early.frame = 20; early.size = 3; early.data[0] = 0x91; early.data[1] = 62; early.data[2] = 90;
    role.onMidi(&early, 1);
    Bytes got;
    for (std::uint32_t off : { 20u, 100u }) {
        role.pumpUntil(off);
        for (int v; (v = role.readFifoByte()) >= 0;) got.push_back(static_cast<std::uint8_t>(v));
    }
    CHECK(got == cat({ { 0xF4, 0 }, frame(0, { 0x91, 62, 90 }), frame(1, { 0x90, 60, 100 }) }));
}
