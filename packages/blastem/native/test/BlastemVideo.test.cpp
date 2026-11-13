// The picture: a Mega Drive's visible area in the triple the shell reads, in the format the triple documents. A frame
// that is not 320 wide, or not the region's height, is a silent failure a min-clamping copy would hide, and a triple
// nobody publishes is the failure the first draft had (framebuffer() returned a member the VDP never drew into). Where
// the picture sits INSIDE the frame is the third case's job, which paints the sink's buffer: a ROM's own picture is a
// uniform backdrop and cannot show where a crop landed.
#include <algorithm>
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

// A source pixel whose colour is its own place in the buffer: row in bits 12-23, column in bits 0-11.
pixel_t pattern(std::uint32_t row, std::uint32_t col) { return 0xFF000000u | (row << 12) | col; }

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
    // Every pixel is an opaque colour (the backdrop of a ROM that never enables a plane), never a zero slot: a zero
    // pixel is what FrameBufferTriple's XRGB8888 contract calls transparent.
    CHECK(std::all_of(pixels.begin(), pixels.end(),
                      [](std::uint32_t p) { return (p & 0xFF000000u) == 0xFF000000u; }));
}

TEST_CASE("a PAL machine's frame is 240 lines", "[blastem][video]") {
    auto sys = boot(rp::blastem::test::fmPsgRom(), 3);
    run(*sys, 4);
    CHECK(sys->coreFrameHeight() == 240);
    CHECK(sys->framebuffer()->height() == 240);
}

TEST_CASE("a picture taller than the frame is centred, not cropped at the bottom", "[blastem][video]") {
    // An NTSC console whose ROM sets the VDP's PAL bit gets inactive_start 0xF0 (240) from update_video_params while the
    // frame is 224 lines tall. 240 lines cannot fit, so the picture is centred in it: the frame shows the middle 224 of
    // them, cropping as much at the top as at the bottom. The buffer is painted here rather than drawn by a ROM, because
    // a ROM's uniform backdrop cannot show which source row a frame row came from.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 4);
    int pitch = 0;
    pixel_t* src = sys->video().buffer(FRAMEBUFFER_EVEN, &pitch);
    REQUIRE(src != nullptr);
    REQUIRE(pitch == LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t)));
    for (std::uint32_t row = 0; row < 512; ++row) {
        for (std::uint32_t col = 0; col < LINEBUF_SIZE; ++col) src[row * LINEBUF_SIZE + col] = pattern(row, col);
    }
    vdp_context* vdp = sys->vdp();
    REQUIRE(vdp != nullptr);
    vdp->regs[REG_MODE_4] |= BIT_H40;
    vdp->inactive_start = 240;
    vdp->border_top = 1;
    sys->video().publish(FRAMEBUFFER_EVEN, 320, *vdp);

    FrameBufferTriple* fb = sys->framebuffer();
    REQUIRE(fb != nullptr);
    std::vector<std::uint32_t> pixels(fb->width() * fb->height());
    REQUIRE(fb->readInto(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
    // dy = (224 - 240) / 2 = -8: the source starts 8 rows into the picture and the frame's last row is the picture's
    // last row. A top-anchored crop would start at border_top + 0 and drop the picture's bottom 16 lines instead.
    CHECK(pixels[0] == pattern(1 + 8, BORDER_LEFT));
    CHECK(pixels[223 * fb->width()] == pattern(1 + 8 + 223, BORDER_LEFT));
}

TEST_CASE("BlastEm's debug framebuffers never replace the published frame", "[blastem][video]") {
    // The debug indices (>= FRAMEBUFFER_USER_START) are 1024-wide rows that nothing in this build enables. Answering them
    // with the odd buffer would let render_framebuffer_updated publish a picture the VDP never finished, so the sink
    // leaves the caller its own landing pad and never publishes one.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    int pitch = 0;
    CHECK(sys->video().buffer(FRAMEBUFFER_USER_START, &pitch) == nullptr);
    run(*sys, 4);
    const std::uint64_t published = sys->video().framesPublished();
    REQUIRE(published > 0);
    sys->video().publish(FRAMEBUFFER_USER_START, 1024, *sys->vdp());
    CHECK(sys->video().framesPublished() == published);
}
