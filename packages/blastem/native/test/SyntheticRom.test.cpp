// The images are code, so this pins them: a hash per image (any byte that moves changes it), the header and vectors
// a Mega Drive needs, and the one property a later case depends on and a hash cannot state (the write table's order).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"

TEST_CASE("the synthetic images are the bytes later cases boot", "[blastem][roms]") {
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::psgOnlyRom()) == 0x6397D2DFu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::fmPsgRom()) == 0x0DCF5FCAu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::fmOnlyRom()) == 0x8F0D759Bu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portPollRom()) == 0xB7DE6771u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portPollLoopRom()) == 0xCBCC6E6Bu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portTimestampRom()) == 0x5EFF37F1u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::syncEdgeRom()) == 0x64D03188u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portLevelRom()) == 0x4338FAB0u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::vdpPollRom()) == 0x8DD1EA23u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::unmappedAccessRom()) == 0xB70FC200u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::hvCounterWriteRom()) == 0x3FD1D443u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::hvPsgRom()) == 0xDCEB58E7u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::vintHintRom()) == 0x3DC48361u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::z80TimingRom()) == 0xA3C8A6BFu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::vintDmaRom()) == 0xBDD1A4BFu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::dmaLoopRom(true)) == 0x8B7DC850u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::dmaLoopRom(false)) == 0x78B66710u);
}

TEST_CASE("an image carries the Mega Drive header and vectors", "[blastem][roms]") {
    const std::vector<std::uint8_t> rom = rp::blastem::test::portPollRom();
    CHECK(rom.size() == 0x10000);
    CHECK(std::memcmp(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16) == 0);
    CHECK(std::memcmp(rom.data(), "\x00\xFF\xFE\x00\x00\x00\x02\x00", 8) == 0);  // SP $00FFFE00, reset $000200
    CHECK(rom[0x200] == 0x10);  // the program's first byte is at $200
}

TEST_CASE("an image's write table is the order the registers expect", "[blastem][roms]") {
    // A hash says a byte moved, not that the table still means what it meant: the YM2612 needs each data byte after its
    // address byte on the same port, and the PSG's three writes must come last in the FM image (latch, high bits,
    // volume). 8 bytes a write, the table first at $200.
    const std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    const std::uint8_t first[8] = {0x13, 0xFC, 0x00, 0x22, 0x00, 0xA0, 0x40, 0x00};
    CHECK(std::memcmp(rom.data() + 0x200, first, 8) == 0);
    const std::size_t last = 0x200 + 8 * 32;  // the 33rd write, the last PSG one
    const std::uint8_t psg[8] = {0x13, 0xFC, 0x00, 0x90, 0x00, 0xC0, 0x00, 0x11};
    CHECK(std::memcmp(rom.data() + last, psg, 8) == 0);
}
