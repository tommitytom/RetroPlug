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
//
// It also covers the rest of the Edio surface a cartridge actually drives — the SD-card file API
// and the NES→host back-channel (CMD_USB_WR) — against the reference the device's own NES SDK
// gives (edn8-pro-pub edio/everdrive.c). Those are the cases below the boot-exchange pair.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

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

void writeU16(rp::NesEverdriveFifo& fifo, std::uint16_t v) {
    writeByte(fifo, static_cast<std::uint8_t>(v & 0xFF));
    writeByte(fifo, static_cast<std::uint8_t>(v >> 8));
}

void writeU32(rp::NesEverdriveFifo& fifo, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) writeByte(fifo, static_cast<std::uint8_t>(v >> (8 * i)));
}

// ed_cmd_usb_wr: the framed command, a u16 length, then the payload.
void writeUsbWr(rp::NesEverdriveFifo& fifo, std::uint8_t cmd, const std::vector<std::uint8_t>& payload) {
    writeCmd(fifo, cmd);
    writeU16(fifo, static_cast<std::uint16_t>(payload.size()));
    for (std::uint8_t b : payload) writeByte(fifo, b);
}

// Pop `n` bytes as the ROM would (poll $40F1 bit 7, then read $40F0). Stops early if the FIFO drains,
// so a short reply shows up as a short vector rather than a stream of 0xFF filler.
std::vector<std::uint8_t> readBytes(rp::NesEverdriveFifo& fifo, std::size_t n) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < n; ++i) {
        if (fifo.ReadRam(0x40F1) == 0x80) break;
        out.push_back(fifo.ReadRam(0x40F0));
    }
    return out;
}

// A scratch directory to stand in for the SD card, removed with the fixture.
struct ScratchCard {
    std::filesystem::path root;
    ScratchCard() {
        root = std::filesystem::temp_directory_path() /
               ("rp-fifo-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);
    }
    ~ScratchCard() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void put(const std::string& name, const std::vector<std::uint8_t>& bytes) const {
        std::ofstream f(root / name, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::vector<std::uint8_t> get(const std::string& name) const {
        std::ifstream f(root / name, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
};

constexpr std::uint8_t CMD_STATUS  = 0x10;
constexpr std::uint8_t CMD_USB_WR  = 0x22;
constexpr std::uint8_t CMD_FIFO_WR = 0x23;
constexpr std::uint8_t CMD_UART_WR = 0x24;
constexpr std::uint8_t CMD_F_FOPN  = 0xC9;
constexpr std::uint8_t CMD_F_FRD   = 0xCA;
constexpr std::uint8_t CMD_F_FWR   = 0xCC;
constexpr std::uint8_t CMD_F_FCLOSE = 0xCE;
constexpr std::uint8_t CMD_F_FPTR  = 0xCF;
constexpr std::uint8_t FA_READ     = 0x01;
constexpr std::uint8_t FA_WRITE    = 0x02;
constexpr std::uint8_t FA_CREATE_ALWAYS = 0x08;
constexpr std::uint8_t FS_MAKEPATH = 0x80;

// A deterministic byte at index i — distinct enough that an off-by-one in the framing shows up.
std::uint8_t patternByte(std::size_t i) { return static_cast<std::uint8_t>((i * 7 + 3) & 0xFF); }

std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = patternByte(i);
    return v;
}

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

// --- The NES→host back-channel (CMD_USB_WR) --------------------------------------------------
//
// The cartridge CAN talk to the host: it writes a framed CMD_USB_WR to $40F0 and the MCU forwards
// the payload out of the USB port (edn8-pro-pub edio/everdrive.c `ed_cmd_usb_wr`, fifo_b in
// fpga/base_sv/base_io.sv). Without this the emulation parsed the command, found no handler, and
// silently dropped every byte — which is what made host-paced streaming look unimplementable.

TEST_CASE("CMD_USB_WR hands its payload to the host and nothing to the ROM", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;

    const std::vector<std::uint8_t> payload{0x01, 0x7F, 0x80, 0xFF, 0x00, 0x2B};
    writeUsbWr(fifo, CMD_USB_WR, payload);

    CHECK(fifo.txCount() == payload.size());
    CHECK(fifo.drainTx() == payload);
    CHECK(fifo.txCount() == 0);          // a drain is a take, not a peek
    CHECK(fifo.drainTx().empty());

    // The ROM's own read channel must not see any of it — the two directions share a register, not
    // a queue. A byte leaking here would decode as MIDI (or as stream data) in the ROM.
    CHECK(fifo.ReadRam(0x40F1) == 0x80);
}

TEST_CASE("CMD_USB_WR accumulates across commands and survives a host-direction flush", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;

    writeUsbWr(fifo, CMD_USB_WR, {0xAA, 0xBB});
    writeUsbWr(fifo, CMD_USB_WR, {0xCC});
    // An empty payload is legal (ed_cmd_fpg_init_usb sends len=1, but len=0 must not wedge the parser).
    writeUsbWr(fifo, CMD_USB_WR, {});
    writeUsbWr(fifo, CMD_USB_WR, {0xDD});

    // clearRx is a barrier in the host→NES direction only: it must not discard what the ROM has
    // already handed the host.
    fifo.pushByte(0x90);
    fifo.clearRx();
    CHECK(fifo.ReadRam(0x40F1) == 0x80);

    const std::vector<std::uint8_t> expected{0xAA, 0xBB, 0xCC, 0xDD};
    CHECK(fifo.drainTx() == expected);
}

TEST_CASE("CMD_FIFO_WR loops back to the ROM, CMD_UART_WR goes nowhere", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;

    // "write to own fifo buffer" — the payload comes back at $40F0.
    writeUsbWr(fifo, CMD_FIFO_WR, {0x11, 0x22, 0x33});
    CHECK(readBytes(fifo, 4) == std::vector<std::uint8_t>{0x11, 0x22, 0x33});
    CHECK(fifo.txCount() == 0);  // not the host's channel

    // No emulated serial port, so the payload is dropped — but it MUST be parsed, or its bytes would
    // fall through to the command parser and the next real command would be lost.
    writeUsbWr(fifo, CMD_UART_WR, {'+', 0xD4, 0x10, 0xEF});
    CHECK(fifo.ReadRam(0x40F1) == 0x80);
    CHECK(fifo.txCount() == 0);

    writeCmd(fifo, CMD_STATUS);
    CHECK(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});
}

// --- The SD-card file API --------------------------------------------------------------------

TEST_CASE("an unset SD root is a scratch card, not the working directory", "[audio][nes][fifo]") {
    // The file exists in the process's CWD. Before, "/" resolved there and the open succeeded, which
    // made a file one test wrote silently become every later run's card.
    const std::string name = "rp-fifo-cwd-probe.bin";
    { std::ofstream f(name, std::ios::binary | std::ios::trunc); f.put('x'); }

    rp::NesEverdriveFifo fifo;  // no setSdRoot
    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, ("/" + name).c_str());
    writeCmd(fifo, CMD_STATUS);
    CHECK(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x04, 0xA5});  // FAT_NO_FILE

    std::error_code ec;
    std::filesystem::remove(name, ec);
}

TEST_CASE("CMD_F_FRD sends one resp byte for the whole request, not one per 512", "[audio][nes][fifo]") {
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(2048);
    card.put("stream.bin", file);

    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);

    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, "/stream.bin");
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    // ed_cmd_file_read reads exactly ONE resp per command it sends. At 512 the old per-block resp was
    // indistinguishable; above it the extra resp bytes decoded as payload and desynced the ROM.
    SECTION("1024 bytes in one command") {
        writeCmd(fifo, CMD_F_FRD);
        writeU32(fifo, 1024);
        const auto reply = readBytes(fifo, 1024 + 8);
        REQUIRE(reply.size() == 1025);
        CHECK(reply[0] == 0x00);
        CHECK(std::vector<std::uint8_t>(reply.begin() + 1, reply.end()) ==
              std::vector<std::uint8_t>(file.begin(), file.begin() + 1024));
    }

    // The SDK's own loop — one command per 512-byte block — must keep working unchanged.
    SECTION("four 512-byte commands walk the file") {
        for (int block = 0; block < 4; ++block) {
            writeCmd(fifo, CMD_F_FRD);
            writeU32(fifo, 512);
            const auto reply = readBytes(fifo, 512 + 8);
            REQUIRE(reply.size() == 513);
            REQUIRE(reply[0] == 0x00);
            CHECK(std::vector<std::uint8_t>(reply.begin() + 1, reply.end()) ==
                  std::vector<std::uint8_t>(file.begin() + block * 512, file.begin() + (block + 1) * 512));
        }
    }

    SECTION("CMD_F_FPTR seeks, and a read past the end reports an error rather than hanging") {
        writeCmd(fifo, CMD_F_FPTR);
        writeU32(fifo, 2040);
        writeCmd(fifo, CMD_STATUS);
        REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

        writeCmd(fifo, CMD_F_FRD);
        writeU32(fifo, 8);
        const auto tail = readBytes(fifo, 16);
        REQUIRE(tail.size() == 9);
        CHECK(tail[0] == 0x00);
        CHECK(tail[8] == patternByte(2047));

        // Now genuinely at EOF. A resp of 0 with no data (the old behaviour) leaves a ROM that polls
        // for its payload waiting forever; a non-zero resp lets it give up.
        writeCmd(fifo, CMD_F_FRD);
        writeU32(fifo, 8);
        CHECK(readBytes(fifo, 16) == std::vector<std::uint8_t>{0x04});
    }
}

TEST_CASE("a read with no file open reports an error instead of a silent success", "[audio][nes][fifo]") {
    rp::NesEverdriveFifo fifo;
    writeCmd(fifo, CMD_F_FRD);
    writeU32(fifo, 16);
    CHECK(readBytes(fifo, 32) == std::vector<std::uint8_t>{0x04});
}

TEST_CASE("CMD_F_FWR stores the payload through the ack handshake", "[audio][nes][fifo]") {
    ScratchCard card;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);

    // FS_MAKEPATH creates the parent dirs, and FA_CREATE_ALWAYS the file — neither of which the
    // emulation used to honour, so a write-open of a new file always failed.
    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH);
    writeString(fifo, "/logs/run/out.bin");
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    // ed_cmd_file_write: read one ack, send up to 1024 bytes, repeat. 2500 bytes = 3 blocks, so the
    // ROM must see exactly 3 acks — and the payload must never reach the command parser.
    const std::vector<std::uint8_t> payload = pattern(2500);
    writeCmd(fifo, CMD_F_FWR);
    writeU32(fifo, static_cast<std::uint32_t>(payload.size()));

    std::size_t sent = 0;
    int acks = 0;
    while (sent < payload.size()) {
        REQUIRE(readBytes(fifo, 1) == std::vector<std::uint8_t>{0x00});  // the device acks first
        ++acks;
        const std::size_t block = std::min<std::size_t>(1024, payload.size() - sent);
        for (std::size_t i = 0; i < block; ++i) writeByte(fifo, payload[sent + i]);
        sent += block;
    }
    CHECK(acks == 3);

    writeCmd(fifo, CMD_STATUS);
    CHECK(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    writeCmd(fifo, CMD_F_FCLOSE);
    writeCmd(fifo, CMD_STATUS);
    CHECK(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    CHECK(card.get("logs/run/out.bin") == payload);

    // And the parser is still in step afterwards: the next command is understood.
    writeCmd(fifo, CMD_STATUS);
    CHECK(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});
}

TEST_CASE("a file written over the FIFO reads back through it byte-for-byte", "[audio][nes][fifo]") {
    ScratchCard card;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);

    const std::vector<std::uint8_t> payload = pattern(700);

    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_WRITE | FA_CREATE_ALWAYS);
    writeString(fifo, "/round.bin");
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    writeCmd(fifo, CMD_F_FWR);
    writeU32(fifo, static_cast<std::uint32_t>(payload.size()));
    REQUIRE(readBytes(fifo, 1) == std::vector<std::uint8_t>{0x00});
    for (std::uint8_t b : payload) writeByte(fifo, b);
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});
    writeCmd(fifo, CMD_F_FCLOSE);
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, "/round.bin");
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});

    writeCmd(fifo, CMD_F_FRD);
    writeU32(fifo, static_cast<std::uint32_t>(payload.size()));
    const auto reply = readBytes(fifo, payload.size() + 8);
    REQUIRE(reply.size() == payload.size() + 1);
    CHECK(reply[0] == 0x00);
    CHECK(std::vector<std::uint8_t>(reply.begin() + 1, reply.end()) == payload);
}
