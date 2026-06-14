#include "system/mesen/MesenVideoDevice.hpp"

#include "transport/FrameBufferTriple.hpp"

void MesenVideoDevice::UpdateFrame(RenderedFrame& frame) {
    if (!fb_ || !frame.FrameBuffer) return;

    // Mesen emits ARGB; the triple-buffer expects 0xFFRRGGBB (alpha forced to
    // 0xFF). Drop the source alpha and re-pack so downstream code (LVGL canvas
    // blits) sees a uniform format.
    const std::uint32_t fbW = fb_->width();
    const std::uint32_t fbH = fb_->height();
    if (frame.Width == 0 || frame.Height == 0) return;

    // If Mesen ever hands us a frame at an unexpected size (NTSC filter, scale)
    // copy the intersecting region; the triple-buffer was sized from the
    // base NES resolution at construction.
    const std::uint32_t copyW = std::min<std::uint32_t>(fbW, frame.Width);
    const std::uint32_t copyH = std::min<std::uint32_t>(fbH, frame.Height);

    std::uint32_t* dst = fb_->writeSlot();
    const std::uint32_t* src = static_cast<const std::uint32_t*>(frame.FrameBuffer);

    for (std::uint32_t y = 0; y < copyH; ++y) {
        const std::uint32_t* srcRow = src + static_cast<std::size_t>(y) * frame.Width;
        std::uint32_t*       dstRow = dst + static_cast<std::size_t>(y) * fbW;
        for (std::uint32_t x = 0; x < copyW; ++x) {
            const std::uint32_t px = srcRow[x];
            dstRow[x] = 0xFF000000u | (px & 0x00FFFFFFu);
        }
    }

    fb_->publish();
}
