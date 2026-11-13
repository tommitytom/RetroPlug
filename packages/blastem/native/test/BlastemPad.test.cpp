// The pad reaches the emulated 6-button controller (design section 5): a press lands on port 1's pad and stays there
// across a block's polls, a release leaves it, and a byte that names no button of this console drives nothing. The read
// is BlastEm's own port state, so what a case pins is the mapping from the wire byte to the pad's bits, not a copy of
// the table pressButton applies from.
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/blastem/InputTypes.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float*         lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

}  // namespace

TEST_CASE("a pressed button is held on port 1's pad", "[blastem][pad]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::portPollRom());
    sys->onActivate(kRate);
    std::vector<float> l(kFrames), r(kFrames);
    sys->pressButton(static_cast<std::uint8_t>(MdButton::A), true);
    // BlastEm's gamepad state: a pad's input bits are inverted at the port, so `input[GAMEPAD_TH0]`'s BUTTON_A bit set
    // means pressed. Read it through the port rather than the device's struct, so the mapping is what is tested.
    runBlock(*sys, l, r);
    CHECK(sys->padDown(static_cast<std::uint8_t>(MdButton::A)));
    sys->pressButton(static_cast<std::uint8_t>(MdButton::A), false);
    runBlock(*sys, l, r);
    CHECK_FALSE(sys->padDown(static_cast<std::uint8_t>(MdButton::A)));
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("every wire byte of the table drives its own button", "[blastem][pad]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::portPollRom());
    sys->onActivate(kRate);
    std::vector<float> l(kFrames), r(kFrames);
    // Each byte holds the button it names and no other, so a table that shifted or doubled up a bit shows up here
    // rather than as a game that reads the wrong pin.
    for (std::uint8_t button = 0; button < static_cast<std::uint8_t>(MdButton::Count); ++button) {
        sys->pressButton(button, true);
        runBlock(*sys, l, r);
        CHECK(sys->padDown(button));
        for (std::uint8_t other = 0; other < static_cast<std::uint8_t>(MdButton::Count); ++other) {
            if (other != button) CHECK_FALSE(sys->padDown(other));
        }
        sys->pressButton(button, false);
        runBlock(*sys, l, r);
        CHECK_FALSE(sys->padDown(button));
    }
    // A byte past the table drives nothing at all rather than falling through onto the pad's last bit.
    sys->pressButton(static_cast<std::uint8_t>(MdButton::Count), true);
    runBlock(*sys, l, r);
    for (std::uint8_t button = 0; button < static_cast<std::uint8_t>(MdButton::Count); ++button) {
        CHECK_FALSE(sys->padDown(button));
    }
}
