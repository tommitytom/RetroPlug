// Guards the emulated EverDrive-N8 Edio FIFO against the "MIDI priming" bug.
//
// n8-midi boots by opening a DMC bank file (CMD_F_FOPN) and then querying the
// result with a *separate* CMD_STATUS (the SDK's `ed_check_status`). The real N8
// MCU stores a command's result internally and only emits it in reply to
// CMD_STATUS. An earlier emulation auto-emitted a status word from FOPN *and*
// from CMD_STATUS, so the ROM read the first and left the second (0x00,0xA5) in
// the RX FIFO. 0xA5 is a valid MIDI status byte (Poly-Aftertouch, ch5): the ROM
// then latched it as a message and swallowed the first two bytes of the next
// real MIDI event — the "a freshly-reset ROM ignores the first message" quirk.
//
// This exercises rp::NesEverdriveFifo directly (no CPU/emulator needed) via its
// $40F0/$40F1 register interface, replaying the boot exchange and asserting the
// FIFO is fully drained afterwards. Run via `pnpm test:plugin` (retroplug-audio-test).

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

#include "system/mesen/NesEverdriveFifo.hpp"

namespace {

void writeByte(rp::NesEverdriveFifo& fifo, std::uint8_t b) { fifo.WriteRam(0x40F0, b); }

// The 4-byte Edio command header the SDK's ed_cmd_tx sends: '+', '+'^0xFF, cmd, cmd^0xFF.
void writeCmd(rp::NesEverdriveFifo& fifo, std::uint8_t cmd) {
    writeByte(fifo, '+');
    writeByte(fifo, static_cast<std::uint8_t>('+' ^ 0xFF));
    writeByte(fifo, cmd);
    writeByte(fifo, static_cast<std::uint8_t>(cmd ^ 0xFF));
}

// ed_tx_string: u16 little-endian length, then the bytes.
void writeString(rp::NesEverdriveFifo& fifo, const char* s) {
    std::uint16_t len = static_cast<std::uint16_t>(std::strlen(s));
    writeByte(fifo, static_cast<std::uint8_t>(len & 0xFF));
    writeByte(fifo, static_cast<std::uint8_t>(len >> 8));
    for (const char* p = s; *p; ++p) writeByte(fifo, static_cast<std::uint8_t>(*p));
}

constexpr std::uint8_t CMD_STATUS = 0x10;
constexpr std::uint8_t CMD_F_FOPN = 0xC9;
constexpr std::uint8_t FA_READ    = 0x01;

} // namespace

TEST_CASE("N8 Edio boot exchange leaves no stale status byte in the MIDI FIFO", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;   // no SD root set → the DMC bank open fails (FAT_NO_FILE), as at boot

    // 1) CMD_F_FOPN <mode> <path> — open a (missing) file, mirroring dmc_load_bank.
    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, "/MIDI/BANK01.DMC");

    // 2) CMD_STATUS — the ROM's ed_check_status queries the result separately.
    writeCmd(fifo, CMD_STATUS);

    // The ROM reads exactly ONE status word (2 bytes) for the whole exchange.
    REQUIRE(fifo.ReadRam(0x40F1) == 0x00);           // bit7 clear = data ready
    CHECK(fifo.ReadRam(0x40F0) == 0x04);             // FAT_NO_FILE (low byte)
    CHECK(fifo.ReadRam(0x40F0) == 0xA5);             // status marker (high byte)

    // The whole point: NOTHING is left over. The old double-status bug left
    // [0x00, 0xA5] here, and 0xA5 desynced the ROM's MIDI parser.
    CHECK(fifo.ReadRam(0x40F1) == 0x80);             // bit7 set = FIFO empty

    // Consequently the first host-MIDI note-on reaches the ROM intact and in order.
    fifo.pushByte(0x90);   // NoteOn ch1
    fifo.pushByte(60);
    fifo.pushByte(100);
    CHECK(fifo.ReadRam(0x40F0) == 0x90);
    CHECK(fifo.ReadRam(0x40F0) == 60);
    CHECK(fifo.ReadRam(0x40F0) == 100);
    CHECK(fifo.ReadRam(0x40F1) == 0x80);             // drained again
}

TEST_CASE("N8 CMD_STATUS reports the most recent command's result", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;

    // A bare CMD_STATUS before any command reports success (0).
    writeCmd(fifo, CMD_STATUS);
    CHECK(fifo.ReadRam(0x40F0) == 0x00);
    CHECK(fifo.ReadRam(0x40F0) == 0xA5);
    CHECK(fifo.ReadRam(0x40F1) == 0x80);

    // After a failing open, CMD_STATUS reports the stored error — and only once.
    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, "/does/not/exist");
    writeCmd(fifo, CMD_STATUS);
    CHECK(fifo.ReadRam(0x40F0) == 0x04);
    CHECK(fifo.ReadRam(0x40F0) == 0xA5);
    CHECK(fifo.ReadRam(0x40F1) == 0x80);
}
