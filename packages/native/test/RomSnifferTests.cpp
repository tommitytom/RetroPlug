#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "system/sameboy/RomSniffer.hpp"

namespace {

// Build a synthetic Game Boy ROM with `title` written into the cartridge
// header at 0x0134 and zero-padded out to a full 32 KiB bank. Real ROMs are
// much larger, but the sniffer only reads the header.
std::vector<std::uint8_t> makeRom(const char* title) {
    std::vector<std::uint8_t> rom(0x8000, 0);
    const std::size_t titleLen = std::strlen(title);
    const std::size_t copyLen  = titleLen > 15 ? 15 : titleLen;
    std::memcpy(rom.data() + 0x0134, title, copyLen);
    return rom;
}

} // namespace

TEST_CASE("detectRomKind returns Mgb for a header titled \"MGB\"", "[RomSniffer]") {
    REQUIRE(detectRomKind(makeRom("MGB")) == RomKind::Mgb);
}

TEST_CASE("detectRomKind returns Lsdj for any \"LSDj\"-prefixed title", "[RomSniffer]") {
    REQUIRE(detectRomKind(makeRom("LSDj"))         == RomKind::Lsdj);
    REQUIRE(detectRomKind(makeRom("LSDj-v9.4.2"))  == RomKind::Lsdj);
    REQUIRE(detectRomKind(makeRom("LSDj-v5.0.3"))  == RomKind::Lsdj);
}

TEST_CASE("detectRomKind returns Generic for unknown titles", "[RomSniffer]") {
    REQUIRE(detectRomKind(makeRom(""))             == RomKind::Generic);
    REQUIRE(detectRomKind(makeRom("POKEMON RED"))  == RomKind::Generic);
    REQUIRE(detectRomKind(makeRom("MGBSOMETHING")) == RomKind::Generic);
    REQUIRE(detectRomKind(makeRom("lsdj"))         == RomKind::Generic); // case-sensitive
}

TEST_CASE("detectRomKind tolerates short ROM buffers without reading past the end",
          "[RomSniffer]") {
    REQUIRE(detectRomKind({})                                   == RomKind::Generic);
    REQUIRE(detectRomKind(std::vector<std::uint8_t>(0x100, 0))  == RomKind::Generic);
    REQUIRE(detectRomKind(std::vector<std::uint8_t>(0x143, 0))  == RomKind::Generic);
}
