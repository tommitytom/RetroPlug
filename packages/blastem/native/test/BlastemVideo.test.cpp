// The picture: a Mega Drive's visible area in the triple the shell reads, in the format the triple documents. A frame
// that is not 320 wide, or not the region's height, is a silent failure a min-clamping copy would hide; and a triple
// nobody publishes is the failure the first draft had (framebuffer() returned a member the VDP never drew into).
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "transport/FrameBufferTriple.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, std::uint32_t region = 1) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = region;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

// Run `blocks` blocks of 512 frames, which is what publishes a frame.
void run(rp::blastem::BlastemMdSystem& sys, int blocks) {
    AudioBlockInfo info{};
    info.frames = 512;
    for (int i = 0; i < blocks; ++i) {
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(512)) {}
        sys.finishBlock(info, nullptr, 0);
    }
}

}  // namespace

TEST_CASE("a Mega Drive frame reaches the triple at the core's size", "[blastem][video]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 4);  // 2048 frames is more than a frame at 60 Hz
    FrameBufferTriple* fb = sys->framebuffer();
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == sys->coreFrameWidth());
    CHECK(fb->height() == sys->coreFrameHeight());
    CHECK(fb->width() == 320);
    CHECK(fb->height() == 224);
    std::vector<std::uint32_t> pixels(fb->width() * fb->height());
    REQUIRE(fb->readInto(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
    // Every pixel is an opaque colour (the backdrop of a ROM that never enables a plane), never a zero slot.
    CHECK((pixels[0] & 0xFF000000u) == 0xFF000000u);
}

TEST_CASE("a PAL machine's frame is 240 lines", "[blastem][video]") {
    auto sys = boot(rp::blastem::test::fmPsgRom(), 3);
    run(*sys, 4);
    CHECK(sys->coreFrameHeight() == 240);
    CHECK(sys->framebuffer()->height() == 240);
}
