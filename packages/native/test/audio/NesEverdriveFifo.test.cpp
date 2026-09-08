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
// It also covers the rest of the Edio surface a cartridge actually drives — the SD-card file API,
// the NES→host back-channel (CMD_USB_WR), and the CMD_F_FRD_MEM DMA with its $40FF handshake —
// against the reference the device's own NES SDK gives (edn8-pro-pub edio/everdrive.c). Those are
// the cases below the boot-exchange pair.
//
// The DMA cases matter more than most: on hardware that command hands the cartridge's memory
// controller to the MCU, so before this emulation existed the only way to check it was to power-cycle
// a console and diff a CHR dump. The wait loop they drive is transcribed from the ROM that measured
// it on real hardware (nesvj src/core/dma.s), so what passes here is what that console executes.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
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
constexpr std::uint8_t CMD_F_FRD_MEM = 0xCB;
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

// Open `path` on the card for reading and confirm it took, so any later failure is about the
// command under test rather than about the file.
void openForRead(rp::NesEverdriveFifo& fifo, const char* path) {
    writeCmd(fifo, CMD_F_FOPN);
    writeByte(fifo, FA_READ);
    writeString(fifo, path);
    writeCmd(fifo, CMD_STATUS);
    REQUIRE(readBytes(fifo, 2) == std::vector<std::uint8_t>{0x00, 0xA5});
}

// The ROM's `ed_check_status`: query the stored result of the last command.
std::uint8_t queryStatus(rp::NesEverdriveFifo& fifo) {
    writeCmd(fifo, CMD_STATUS);
    const auto reply = readBytes(fifo, 2);
    REQUIRE(reply.size() == 2);
    REQUIRE(reply[1] == 0xA5);
    return reply[0];
}

// --- CMD_F_FRD_MEM: the DMA into cartridge memory --------------------------------------------

constexpr std::uint16_t REG_MSTAT      = 0x40FF;
constexpr std::uint8_t  MSTAT_MCU_PEND = 0x02;
constexpr std::uint8_t  MSTAT_FPG_PEND = 0x04;
constexpr std::uint8_t  MSTAT_STROBE   = 0x08;

// The destination window MesenNesSystem backs with Mesen's CHR-RAM.
constexpr std::uint32_t PI_CHR_RAM = 0x00C00000;

// PAL, which is what the console that measured the DMA runs, and the cost of one pass of its wait
// loop: 4+3+4+2+3+2 +2+3+3+3 +5+3, counted off nesvj src/core/dma.s.
constexpr std::uint32_t PAL_CLOCK_HZ   = 1662607;
constexpr std::uint64_t CYCLES_PER_SPIN = 37;

// Stands in for the cartridge memory a DMA lands in. The writer has the same contract as the real
// one in MesenNesSystem: validate the WHOLE range first, so a refusal leaves memory untouched.
struct FakeCart {
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(32 * 1024, 0x00);

    std::function<bool(std::uint32_t, const std::uint8_t*, std::size_t)> writer() {
        return [this](std::uint32_t piAddr, const std::uint8_t* data, std::size_t len) {
            if (piAddr < PI_CHR_RAM) return false;
            const std::uint64_t off = piAddr - PI_CHR_RAM;
            if (off + len > mem.size()) return false;
            std::memcpy(mem.data() + off, data, len);
            return true;
        };
    }

    bool untouched() const {
        for (std::uint8_t b : mem) if (b != 0x00) return false;
        return true;
    }

    std::vector<std::uint8_t> at(std::size_t off, std::size_t len) const {
        return std::vector<std::uint8_t>(mem.begin() + off, mem.begin() + off + len);
    }
};

// The bytes the ROM sends, in the order it sends them: the framed command, the two u32 parameters,
// the ARM write to $40FF, and only then the exec write to $40F0.
void writeDmaRequest(rp::NesEverdriveFifo& fifo, std::uint32_t addr, std::uint32_t len) {
    writeCmd(fifo, CMD_F_FRD_MEM);
    writeU32(fifo, addr);
    writeU32(fifo, len);
    fifo.WriteRam(REG_MSTAT, MSTAT_MCU_PEND);
    fifo.WriteRam(0x40F0, 0x00);
}

// One pass of the ROM's wait loop, transcribed instruction for instruction from dma.s. All three
// tests it applies, in its order: the two reads differ in nothing but the strobe, the armed bits
// read clear, and the high nibble reads $A.
bool mstatSettled(rp::NesEverdriveFifo& fifo, std::uint8_t want) {
    const std::uint8_t v1 = fifo.ReadRam(REG_MSTAT);                    // lda REG_MSTAT / sta dma_prev
    const std::uint8_t v2 = fifo.ReadRam(REG_MSTAT);                    // lda REG_MSTAT
    std::uint8_t a = static_cast<std::uint8_t>(v2 ^ MSTAT_STROBE);      // eor #STAT_STROBE
    if (a != v1) return false;                                          // cmp dma_prev / bne again
    a = static_cast<std::uint8_t>(a ^ (MSTAT_MCU_PEND | MSTAT_FPG_PEND));
    if (static_cast<std::uint8_t>(a & want) != want) return false;      // and dma_want / cmp / bne
    return (v1 & 0xF0) == 0xA0;                                         // and #$F0 / cmp #$A0
}

// The whole loop, bounded the way the ROM bounds it (4096 spins). Returns how many passes failed
// before it settled, or -1 if it gave up — which is what a wedged handshake looks like on a console.
int spinForDma(rp::NesEverdriveFifo& fifo, std::uint8_t want = MSTAT_MCU_PEND) {
    for (int spins = 0; spins < 4096; ++spins) {
        if (mstatSettled(fifo, want)) return spins;
    }
    return -1;
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

// --- CMD_F_FRD_MEM ----------------------------------------------------------------------------
//
// The MCU reads the open file straight into cartridge memory over the PI bus, with the 6502 doing
// nothing but waiting on $40FF. Verified on a real N8 Pro from a running game (nesvj 1ca7107):
// 8192 bytes into CHR-RAM bank 0, byte-exact against a dump off the cart.

TEST_CASE("CMD_F_FRD_MEM writes the file straight into cartridge memory", "[audio][nes][fifo][dma]") {
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(4096);
    card.put("clip.bin", file);

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());

    openForRead(fifo, "/clip.bin");

    SECTION("at the base of the window") {
        writeDmaRequest(fifo, PI_CHR_RAM, 4096);
        // No clock source, so the transfer is instant and the loop settles on its first pass.
        CHECK(spinForDma(fifo) == 0);
        // Unlike CMD_F_FRD this command replies with no bytes at all — the payload went to memory.
        CHECK(fifo.ReadRam(0x40F1) == 0x80);
        CHECK(queryStatus(fifo) == 0);
        CHECK(cart.at(0, 4096) == file);
    }

    SECTION("at an offset inside it") {
        writeDmaRequest(fifo, PI_CHR_RAM + 0x2000, 4096);
        CHECK(spinForDma(fifo) == 0);
        CHECK(queryStatus(fifo) == 0);
        CHECK(cart.at(0x2000, 4096) == file);
        // The address is ABSOLUTE: nothing outside the requested range moved.
        CHECK(cart.at(0, 0x2000) == std::vector<std::uint8_t>(0x2000, 0x00));
    }
}

TEST_CASE("CMD_F_FRD_MEM advances the file pointer, so F_FRD carries on from it", "[audio][nes][fifo][dma]") {
    // The property nesvj's frame loop depends on: a small per-frame header over the FIFO and the
    // 4 KB image by DMA, interleaved against ONE pointer, with both advancing it.
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(8192);
    card.put("clip.bin", file);

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());

    openForRead(fifo, "/clip.bin");
    writeDmaRequest(fifo, PI_CHR_RAM, 4096);
    REQUIRE(spinForDma(fifo) == 0);
    REQUIRE(queryStatus(fifo) == 0);

    writeCmd(fifo, CMD_F_FRD);
    writeU32(fifo, 64);
    const auto reply = readBytes(fifo, 64 + 8);
    REQUIRE(reply.size() == 65);
    CHECK(reply[0] == 0x00);
    CHECK(std::vector<std::uint8_t>(reply.begin() + 1, reply.end()) ==
          std::vector<std::uint8_t>(file.begin() + 4096, file.begin() + 4160));

    // And back the other way: a DMA after an F_FRD starts where the F_FRD stopped.
    writeDmaRequest(fifo, PI_CHR_RAM + 0x1000, 128);
    CHECK(spinForDma(fifo) == 0);
    CHECK(queryStatus(fifo) == 0);
    CHECK(cart.at(0x1000, 128) == std::vector<std::uint8_t>(file.begin() + 4160, file.begin() + 4288));
}

TEST_CASE("$40FF answers the shape the SDK's wait loop tests", "[audio][nes][fifo][dma]") {
    rp::NesEverdriveFifo fifo;   // idle: nothing armed, nothing staged

    const std::uint8_t a = fifo.ReadRam(REG_MSTAT);
    const std::uint8_t b = fifo.ReadRam(REG_MSTAT);
    // The loop's first test. It reads the register TWICE per pass, so the strobe has to flip per
    // read — per pass or per frame would fail it every time.
    CHECK(static_cast<std::uint8_t>(a ^ b) == MSTAT_STROBE);
    CHECK((a & 0xF0) == 0xA0);   // the loop's third test: the reply is well-formed
    CHECK((a & 0x06) == 0);      // idle reads as idle, so a ROM polling with no DMA out doesn't hang

    SECTION("the peek path is side-effect-free") {
        // A memory viewer refreshing $40FF must not move the strobe: it would change what the ROM's
        // next read sees, and could satisfy the wait loop on the ROM's behalf.
        const std::uint8_t peek = fifo.PeekRam(REG_MSTAT);
        CHECK(fifo.PeekRam(REG_MSTAT) == peek);
        CHECK(fifo.PeekRam(REG_MSTAT) == peek);
        CHECK(fifo.ReadRam(REG_MSTAT) == peek);
    }

    SECTION("a write arms the pending bits, and only those bits") {
        fifo.WriteRam(REG_MSTAT, 0xFF);
        CHECK((fifo.ReadRam(REG_MSTAT) & ~MSTAT_STROBE) == (0xA0 | MSTAT_MCU_PEND | MSTAT_FPG_PEND));
        fifo.WriteRam(REG_MSTAT, 0x00);
        CHECK((fifo.ReadRam(REG_MSTAT) & ~MSTAT_STROBE) == 0xA0);
    }

    SECTION("arming with nothing behind it never settles") {
        // As on a device where nothing was triggered. This is the reason the EXEC write completes a
        // transfer and the parameters do not: only an executed DMA can retire the pending bit.
        fifo.WriteRam(REG_MSTAT, MSTAT_MCU_PEND);
        CHECK(spinForDma(fifo) == -1);
    }
}

TEST_CASE("the DMA runs on the exec write, not on the last parameter byte", "[audio][nes][fifo][dma]") {
    // THE ordering trap. The ROM arms $40FF *after* the parameters, so an implementation that copies
    // when the 8th parameter byte lands clears the pending bit before the arming write sets it —
    // and nothing is left to clear it again. The ROM spins to its bound and reports failure for a
    // transfer that actually happened.
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(256);
    card.put("clip.bin", file);

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());
    openForRead(fifo, "/clip.bin");

    writeCmd(fifo, CMD_F_FRD_MEM);
    writeU32(fifo, PI_CHR_RAM);
    writeU32(fifo, 256);
    CHECK(cart.untouched());                 // staged only

    fifo.WriteRam(REG_MSTAT, MSTAT_MCU_PEND);   // arm, AFTER the parameters
    CHECK(cart.untouched());
    CHECK((fifo.ReadRam(REG_MSTAT) & MSTAT_MCU_PEND) == MSTAT_MCU_PEND);

    fifo.WriteRam(0x40F0, 0x00);                // exec
    CHECK(spinForDma(fifo) == 0);
    CHECK(queryStatus(fifo) == 0);
    CHECK(cart.at(0, 256) == file);

    // The exec byte was consumed by the DMA path rather than fed to the command parser, so the
    // parser is still in step: an ordinary framed command lands, and so does a second DMA.
    writeCmd(fifo, CMD_F_FPTR);
    writeU32(fifo, 0);
    CHECK(queryStatus(fifo) == 0);

    writeDmaRequest(fifo, PI_CHR_RAM + 0x100, 64);
    CHECK(spinForDma(fifo) == 0);
    CHECK(queryStatus(fifo) == 0);
    CHECK(cart.at(0x100, 64) == std::vector<std::uint8_t>(file.begin(), file.begin() + 64));
}

TEST_CASE("a DMA to memory that isn't backed fails, and writes nothing", "[audio][nes][fifo][dma]") {
    // A silent drop would look EXACTLY like a working DMA to the ROM, which is the worst failure
    // mode available here — so an unbacked destination has to be an Edio error.
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(512);
    card.put("clip.bin", file);

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());
    openForRead(fifo, "/clip.bin");

    std::uint32_t addr = 0;
    std::uint32_t len  = 64;
    SECTION("below the window")       { addr = PI_CHR_RAM - 1; }
    SECTION("overrunning its end")    { addr = PI_CHR_RAM + 32 * 1024 - 8; }
    SECTION("PRG, a legal PI target on the device")  { addr = 0x0000000; }
    SECTION("SRAM, likewise")                        { addr = 0x1000000; }

    writeDmaRequest(fifo, addr, len);
    // The MCU still ANSWERED — "it replied" and "it did what was asked" are separate questions, and
    // the ROM asks the second one with CMD_STATUS. A loop that hung here could not report anything.
    CHECK(spinForDma(fifo) == 0);
    CHECK(queryStatus(fifo) != 0);
    CHECK(cart.untouched());

    // The file pointer did not move either, so the next read still starts at byte 0 and a ROM can
    // fall back to the CPU-mediated path without re-seeking.
    writeCmd(fifo, CMD_F_FRD);
    writeU32(fifo, 4);
    const auto reply = readBytes(fifo, 8);
    REQUIRE(reply.size() == 5);
    CHECK(std::vector<std::uint8_t>(reply.begin() + 1, reply.end()) ==
          std::vector<std::uint8_t>(file.begin(), file.begin() + 4));
}

TEST_CASE("a DMA with nowhere to go, or nothing to read, is an error", "[audio][nes][fifo][dma]") {
    ScratchCard card;
    card.put("clip.bin", pattern(64));

    SECTION("no cart writer installed at all") {
        // The shape any host that isn't wired to a core has. It must report failure rather than
        // crash or claim success.
        rp::NesEverdriveFifo fifo;
        fifo.setSdRoot(card.root);
        openForRead(fifo, "/clip.bin");
        writeDmaRequest(fifo, PI_CHR_RAM, 64);
        CHECK(spinForDma(fifo) == 0);
        CHECK(queryStatus(fifo) != 0);
    }

    SECTION("no open file") {
        FakeCart cart;
        rp::NesEverdriveFifo fifo;
        fifo.setSdRoot(card.root);
        fifo.setCartWriter(cart.writer());
        writeDmaRequest(fifo, PI_CHR_RAM, 64);
        CHECK(spinForDma(fifo) == 0);
        CHECK(queryStatus(fifo) == 0x04);   // FAT_NO_FILE
        CHECK(cart.untouched());
    }

    SECTION("a length no destination could absorb") {
        // A garbled parameter must be refused before it is believed: sizing a staging buffer from a
        // u32 taken at face value would try to allocate 4 GB inside the CPU's write handler.
        FakeCart cart;
        rp::NesEverdriveFifo fifo;
        fifo.setSdRoot(card.root);
        fifo.setCartWriter(cart.writer());
        openForRead(fifo, "/clip.bin");
        writeDmaRequest(fifo, PI_CHR_RAM, 0xFFFFFFFFu);
        CHECK(spinForDma(fifo) == 0);
        CHECK(queryStatus(fifo) != 0);
        CHECK(cart.untouched());
    }

    SECTION("already at EOF") {
        FakeCart cart;
        rp::NesEverdriveFifo fifo;
        fifo.setSdRoot(card.root);
        fifo.setCartWriter(cart.writer());
        openForRead(fifo, "/clip.bin");

        writeDmaRequest(fifo, PI_CHR_RAM, 64);
        REQUIRE(spinForDma(fifo) == 0);
        REQUIRE(queryStatus(fifo) == 0);

        // Nothing left. A success with no data would leave a ROM re-displaying the same 64 bytes
        // forever and never learning the stream had ended.
        writeDmaRequest(fifo, PI_CHR_RAM + 0x100, 64);
        CHECK(spinForDma(fifo) == 0);
        CHECK(queryStatus(fifo) == 0x04);
    }
}

TEST_CASE("the transfer holds the pending bit for its modelled duration", "[audio][nes][fifo][dma]") {
    // Without a modelled duration the wait loop exits on its first pass, and a frame budget measured
    // against the emulator comes out optimistic — the DMA looks free when on the device it is not.
    ScratchCard card;
    const std::vector<std::uint8_t> file = pattern(8192);
    card.put("clip.bin", file);

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());

    std::uint64_t cycles = 0;
    fifo.setDmaClock([&cycles] { return cycles; }, PAL_CLOCK_HZ);
    openForRead(fifo, "/clip.bin");

    writeDmaRequest(fifo, PI_CHR_RAM, 8192);

    // Busy the instant the exec write lands...
    CHECK((fifo.ReadRam(REG_MSTAT) & MSTAT_MCU_PEND) == MSTAT_MCU_PEND);
    // ...but the bytes are already in place. The emulation lands them atomically at the start of the
    // window rather than progressively; no ROM can tell, because the CPU must not touch the cart
    // while the MCU holds it (which is why the SDK's wait loop runs from RAM).
    CHECK(cart.at(0, 8192) == file);

    const auto window = static_cast<std::uint64_t>(
        8192.0 / rp::DMA_BYTES_PER_SECOND * static_cast<double>(PAL_CLOCK_HZ));
    cycles = window - 1;
    CHECK((fifo.ReadRam(REG_MSTAT) & MSTAT_MCU_PEND) == MSTAT_MCU_PEND);
    cycles = window;
    CHECK((fifo.ReadRam(REG_MSTAT) & MSTAT_MCU_PEND) == 0);
    CHECK(queryStatus(fifo) == 0);
}

TEST_CASE("the modelled wait costs the ROM about as many spins as the console measured", "[audio][nes][fifo][dma]") {
    // The measurement this is calibrated against: a real N8 Pro moved 8192 bytes in 165 and 172
    // passes of this loop, over two boots. Drive the transcribed loop with a clock that advances the
    // way the 6502 does and the emulated console should land in the same place.
    ScratchCard card;
    card.put("clip.bin", pattern(8192));

    FakeCart cart;
    rp::NesEverdriveFifo fifo;
    fifo.setSdRoot(card.root);
    fifo.setCartWriter(cart.writer());

    std::uint64_t cycles = 0;
    fifo.setDmaClock([&cycles] { return cycles; }, PAL_CLOCK_HZ);
    openForRead(fifo, "/clip.bin");
    writeDmaRequest(fifo, PI_CHR_RAM, 8192);

    int spins = 0;
    for (; spins < 4096; ++spins) {
        if (mstatSettled(fifo, MSTAT_MCU_PEND)) break;
        cycles += CYCLES_PER_SPIN;
    }
    CHECK(spins >= 140);
    CHECK(spins <= 200);

    // And the model scales: a quarter of the bytes is a quarter of the wait, which is what makes a
    // per-frame budget computable from it at all.
    cycles = 0;
    writeDmaRequest(fifo, PI_CHR_RAM, 2048);
    int shortSpins = 0;
    for (; shortSpins < 4096; ++shortSpins) {
        if (mstatSettled(fifo, MSTAT_MCU_PEND)) break;
        cycles += CYCLES_PER_SPIN;
    }
    CHECK(shortSpins >= spins / 5);
    CHECK(shortSpins <= spins / 3);
}
