#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, std::uint32_t region = 1) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = region;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

}  // namespace

TEST_CASE("a Mega Drive image boots into a live core", "[blastem][boot]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    CHECK(sys->activated());
    CHECK(sys->masterClock() == 53693175u);
    CHECK(sys->coreFrameWidth() == 320);
    CHECK(sys->coreFrameHeight() == 224);
}

TEST_CASE("a PAL region changes the clock and the picture's height", "[blastem][boot]") {
    auto sys = boot(rp::blastem::test::fmPsgRom(), 3 /* pal */);
    CHECK(sys->masterClock() == 53203395u);
    CHECK(sys->coreFrameHeight() == 240);
}

TEST_CASE("the gate refuses what is not this console", "[blastem][boot]") {
    std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    rom[0x100] = 'X';
    CHECK_FALSE(rp::blastem::isMdRom(rom));                     // no "SEGA" at all
    std::vector<std::uint8_t> mars = rp::blastem::test::fmPsgRom();
    std::memcpy(mars.data() + 0x100, "SEGA 32X        ", 16);
    CHECK_FALSE(rp::blastem::isMdRom(mars));                    // the 32X's own magic: another console
    CHECK(rp::blastem::isMdRom(rp::blastem::test::fmPsgRom()));  // and the real thing still passes
}
