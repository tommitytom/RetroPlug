// What the control plane reads and writes (design section 5): the 68k's work RAM at 68k addresses (BlastEm stores it as
// 16-bit words, so a naive view is byte-swapped), the Z80's own 8 KiB, and the cartridge battery in BlastEm's own
// odd-byte layout (byte i is address $200001 + 2i), which is what a real odd-byte cart holds and what genmddj's sync
// variables are read through.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys) {
    std::vector<float>   l(kFrames), r(kFrames);
    float*               lanes[2] = {l.data(), r.data()};
    AudioBlockInfo       info{};
    info.frames = kFrames;
    // The engine zeroes the lanes it hands over (SystemBase's output contract), and finishBlock adds into them.
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

// A cart with 64 KiB of odd-byte SRAM, as genmddj's header declares it ($1B0 "RA", $F8, $20; start $200001, end
// $21FFFF), and a program that writes one byte into it at $200001 and reads it back into work RAM at $FF0000.
std::vector<std::uint8_t> sramRom() {
    std::vector<std::uint8_t> rom = rp::blastem::test::image(
        {0x13, 0xFC, 0x00, 0x5A, 0x00, 0x20, 0x00, 0x01,  // move.b #$5A,$00200001
         0x10, 0x39, 0x00, 0x20, 0x00, 0x01,              // move.b $00200001,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00},             // move.b d0,$FF0000
        {});
    rom[0x1B0] = 'R';
    rom[0x1B1] = 'A';
    rom[0x1B2] = 0xF8;  // SRAM present, odd bytes
    rom[0x1B3] = 0x20;
    rom[0x1B4] = 0x00; rom[0x1B5] = 0x20; rom[0x1B6] = 0x00; rom[0x1B7] = 0x01;  // start $00200001
    rom[0x1B8] = 0x00; rom[0x1B9] = 0x21; rom[0x1BA] = 0xFF; rom[0x1BB] = 0xFF;  // end $0021FFFF
    return rom;
}

// A cart with 64 KiB of 16-bit (BOTH) SRAM: the flags byte $1B2 is $02, so its $18 bits are clear and BlastEm keeps the
// battery in big-endian word order, pair-swapped against every other emulator's .sav (sega_mapper.c's read_sram_b,
// and genesis.c's swap around its own file write). This is the common 16-bit SRAM cart, and the one the odd-byte case
// cannot speak for.
std::vector<std::uint8_t> sramBothHeader(std::vector<std::uint8_t> rom) {
    rom[0x1B0] = 'R';
    rom[0x1B1] = 'A';
    rom[0x1B2] = 0x02;  // SRAM present, both bytes ($1B2 & $18 == 0)
    rom[0x1B3] = 0x20;
    rom[0x1B4] = 0x00; rom[0x1B5] = 0x20; rom[0x1B6] = 0x00; rom[0x1B7] = 0x00;  // start $00200000
    rom[0x1B8] = 0x00; rom[0x1B9] = 0x20; rom[0x1BA] = 0xFF; rom[0x1BB] = 0xFF;  // end $0020FFFF
    return rom;
}

// Writes one byte to each parity of the same word, then reads both back into work RAM at $FF0000 and $FF0001, so the
// cart's own byte order is visible from the 68k's side as well as from the dump.
std::vector<std::uint8_t> sramBothRom() {
    return sramBothHeader(rp::blastem::test::image(
        {0x13, 0xFC, 0x00, 0xA5, 0x00, 0x20, 0x00, 0x00,  // move.b #$A5,$00200000
         0x13, 0xFC, 0x00, 0x5A, 0x00, 0x20, 0x00, 0x01,  // move.b #$5A,$00200001
         0x10, 0x39, 0x00, 0x20, 0x00, 0x00,              // move.b $00200000,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00,              // move.b d0,$FF0000
         0x10, 0x39, 0x00, 0x20, 0x00, 0x01,              // move.b $00200001,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x01},             // move.b d0,$FF0001
        {}));
}

// The same cart with a program that only reads both bytes, so a case can see what a battery seed put there.
std::vector<std::uint8_t> sramBothReadRom() {
    return sramBothHeader(rp::blastem::test::image(
        {0x10, 0x39, 0x00, 0x20, 0x00, 0x00,              // move.b $00200000,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00,              // move.b d0,$FF0000
         0x10, 0x39, 0x00, 0x20, 0x00, 0x01,              // move.b $00200001,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x01},             // move.b d0,$FF0001
        {}));
}

}  // namespace

TEST_CASE("work RAM reads at 68k addresses, not in BlastEm's word order", "[blastem][memory]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    rp::MemoryAccessor ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    CHECK(ram.size() == 0x10000);
    // portPollRom stores what it read from $A10005 at $FF0000; work RAM's offset 0 IS $FF0000's byte.
    CHECK(ram[0] != 0x00);  // the pad's idle $7F (0x7F & 0xFF), not the unused odd byte
    CHECK((ram[0] & 0x40) != 0);
}

TEST_CASE("the Z80's own RAM is readable, and not a listed region", "[blastem][memory]") {
    // The registry publishes the work RAM every block, and the engine only reads a published region (a write to any
    // other is refused for its published size of 0), so listing the Z80's RAM would advertise a region that every job
    // naming it is refused on. It stays reachable through the accessor for whoever publishes it next.
    auto                 sys = boot(rp::blastem::test::portPollRom());
    const auto           regions = sys->memory()->regions();
    REQUIRE(regions.size() == 1);
    CHECK(std::string(regions[0].name) == "work");
    CHECK(regions[0].type == rp::MemoryType::Ram);
    rp::MemoryAccessor z80 = sys->getMemory(rp::MemoryType::ExtWorkRam, rp::AccessType::Read);
    CHECK(z80.valid());
    CHECK(z80.size() == 8192);
}

TEST_CASE("the work RAM accessor is a read-only copy", "[blastem][memory]") {
    // The Ram view is a copy in address order, so a write through it would succeed and change nothing in the core.
    // A caller has to see that from the accessor's own access type, or a poke that reads back through the same copy
    // looks like it worked; memory() is the write half.
    auto sys = boot(rp::blastem::test::portPollRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    rp::MemoryAccessor acc = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::ReadWrite);
    REQUIRE(acc.valid());
    CHECK(acc.access() == rp::AccessType::Read);
    CHECK_FALSE(acc.write(0x0100, std::uint8_t(0x5A)));
    CHECK(sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read)[0x0100] == 0x00);
}

TEST_CASE("a capability write refuses a range past the work RAM", "[blastem][memory]") {
    // The bound is the region's own size (RAM_WORDS words), not a copy of it: half a song block is not a song, and a
    // write past the end would corrupt whatever the core keeps after it.
    auto                  sys = boot(rp::blastem::test::portPollRom());
    rp::MemoryCapability* memory = sys->memory();
    REQUIRE(memory != nullptr);
    const std::uint8_t bytes[4] = {0x11, 0x22, 0x33, 0x44};
    CHECK(memory->write(0, 0xFFFE, bytes, 2));         // the last word
    CHECK_FALSE(memory->write(0, 0xFFFF, bytes, 2));   // one byte past it
    CHECK_FALSE(memory->write(0, 0x10000, bytes, 1));  // the offset itself past it
    const rp::MemoryAccessor ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    CHECK(ram[0xFFFE] == 0x11);
    CHECK(ram[0xFFFF] == 0x22);
}

TEST_CASE("a capability write lands at the 68k's address", "[blastem][memory]") {
    auto             sys = boot(rp::blastem::test::portPollRom());
    rp::MemoryCapability* memory = sys->memory();
    REQUIRE(memory != nullptr);
    const std::uint8_t value = 0x5A;
    CHECK(memory->write(0, 0x0100, &value, 1));  // $FF0100
    CHECK(sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read)[0x0100] == 0x5A);
}

TEST_CASE("the battery keeps BlastEm's odd-byte layout", "[blastem][memory]") {
    auto sys = boot(sramRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE(sram.size() == 0x10000);  // (end - start + 1) / 2: the compacted odd-byte cart
    CHECK(sram[0] == 0x5A);           // byte 0 is $200001, which the ROM wrote
    CHECK(sram[1] == 0x00);           // byte 1 is $200003
    // And the ROM read its own byte back through the odd lane: the whole mask, not bits of it (0xFF would pass a mask).
    CHECK(sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read)[0] == 0x5A);
}

TEST_CASE("the battery of a 16-bit cart is exported in the .sav's order", "[blastem][memory]") {
    // BlastEm's storage for a RAM_FLAG_BOTH cart is the file's pairs swapped (the byte at address a is save_storage[a^1]),
    // and its own save path swaps them back around the write. So a dump of the raw buffer is rotated against every
    // .sav, and a dump that is not means an exported battery no other emulator can read.
    auto sys = boot(sramBothRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE(sram.size() == 0x10000);
    CHECK(sram[0] == 0xA5);  // $200000, which the ROM wrote
    CHECK(sram[1] == 0x5A);  // $200001
    // The raw storage really is the other way round, which is why the dump is a copy and not the buffer.
    const auto* raw = static_cast<const std::uint8_t*>(static_cast<const void*>(sys->core()->save_storage));
    CHECK(raw[0] == 0x5A);
    CHECK(raw[1] == 0xA5);
    // What the registry publishes is the same order as the dump, so a reader and a save agree.
    const rp::MemoryAccessor battery = sys->getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    REQUIRE(battery.valid());
    REQUIRE(battery.size() == sram.size());
    CHECK(std::equal(sram.begin(), sram.end(), battery.data()));
    // And the 68k read both bytes at the addresses it wrote them to.
    const rp::MemoryAccessor ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    CHECK(ram[0] == 0xA5);
    CHECK(ram[1] == 0x5A);
}

TEST_CASE("a 16-bit cart's battery seed lands in the .sav's order", "[blastem][memory]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    cfg.sram.assign(0x10000, 0x00);
    cfg.sram[0] = 0x11;  // $200000
    cfg.sram[1] = 0x22;  // $200001
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, sramBothReadRom());
    sys->onActivate(kRate);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE(sram.size() == 0x10000);
    CHECK(sram[0] == 0x11);
    CHECK(sram[1] == 0x22);
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    const rp::MemoryAccessor ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    CHECK(ram[0] == 0x11);  // what the ROM read back from $200000
    CHECK(ram[1] == 0x22);  // and from $200001
}

TEST_CASE("a live battery write lands in the .sav's order", "[blastem][memory]") {
    // rp.system.sram/1's one live write (the host saving a song into a running cart's battery). CoreSram writes through
    // getMemory(Sram), which here is the address-ordered copy, so this core's capability has to do the pairing itself.
    auto sys = boot(sramBothReadRom());
    rp::SramCapability* battery = sys->sram();
    REQUIRE(battery != nullptr);
    const std::uint8_t bytes[2] = {0xA5, 0x5A};
    CHECK(battery->write(0, bytes, 2, nullptr) == rp::SramCapability::WriteResult::Written);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE(sram.size() == 0x10000);
    CHECK(sram[0] == 0xA5);
    CHECK(sram[1] == 0x5A);
    // The compare-and-swap half compares in the caller's order too, and a stale expectation writes nothing.
    CHECK(battery->write(0, bytes, 2, bytes) == rp::SramCapability::WriteResult::Written);
    const std::uint8_t stale[1] = {0x00};
    CHECK(battery->write(0, bytes, 1, stale) == rp::SramCapability::WriteResult::Changed);
    CHECK(sys->saveSramBytes()[0] == 0xA5);
    // Past the end is refused, as the capability says.
    CHECK(battery->write(0xFFFE, bytes, 4, nullptr) == rp::SramCapability::WriteResult::OutOfRange);
}

TEST_CASE("a battery seed reaches the cart", "[blastem][memory]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    cfg.sram.assign(0x10000, 0x00);
    cfg.sram[0] = 0xA5;  // $200001
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, sramRom());
    sys->onActivate(kRate);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE_FALSE(sram.empty());  // indexing an empty dump would be UB, not this case's failure
    CHECK(sram[0] == 0xA5);
}
