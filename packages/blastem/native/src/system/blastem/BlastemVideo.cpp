#include "system/blastem/BlastemVideo.hpp"

#include <algorithm>

#include "transport/FrameBufferTriple.hpp"

namespace rp::blastem {

static_assert(sizeof(pixel_t) == 4, "the frame triple is XRGB8888: BlastEm must be built without USE_RGB565");

VideoSink::~VideoSink() = default;

void VideoSink::prepareBuffers() {
    // The VDP's bordered frame is LINEBUF_SIZE wide and under 280 rows tall; kRows is 512, the size the VDP's headless
    // allocation uses, and it is a CEILING on the row its own code can write, not a mode's height.
    odd_.assign(static_cast<std::size_t>(LINEBUF_SIZE) * kRows, 0u);
    even_.assign(static_cast<std::size_t>(LINEBUF_SIZE) * kRows, 0u);
    published_ = 0;
}

void VideoSink::prepareFrame(std::uint32_t regionLines) {
    triple_ = std::make_unique<FrameBufferTriple>(320u, regionLines);
}

std::uint32_t VideoSink::width() const { return triple_ ? triple_->width() : 0u; }
std::uint32_t VideoSink::height() const { return triple_ ? triple_->height() : 0u; }

pixel_t* VideoSink::buffer(std::uint8_t which, int* pitch) {
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    // BlastEm's debug indices are not this sink's buffers: a null sends the caller to its own landing pad, where the
    // debugger's 1024-wide rows are harmless, instead of handing back the odd buffer under a name publish() must not
    // treat as a frame.
    if (which >= FRAMEBUFFER_USER_START) return nullptr;
    std::vector<pixel_t>& buf = which == FRAMEBUFFER_EVEN ? even_ : odd_;
    return buf.empty() ? nullptr : buf.data();
}

void VideoSink::publish(std::uint8_t which, int /*width*/, const vdp_context& vdp) {
    if (!triple_ || odd_.empty() || which >= FRAMEBUFFER_USER_START) return;
    // The picture is the VDP's own mode, not the pitch BlastEm hands over: 320 in H40, 256 in H32, and the mode's own
    // visible lines (inactive_start: 224 or 240 in mode 5, 192 in mode 4). It is CENTRED in the fixed frame, and every
    // frame pixel the picture does not cover is filled with the backdrop below, so a mode change moves the picture
    // instead of resizing the frame.
    const std::uint32_t w = (vdp.regs[REG_MODE_4] & BIT_H40) ? 320u : 256u;
    const std::uint32_t h = vdp.inactive_start;
    const std::uint32_t frameW = triple_->width();
    const std::uint32_t frameH = triple_->height();
    const std::uint32_t left = (frameW - std::min(w, frameW)) / 2;
    // A picture taller than the frame (an NTSC console whose ROM sets the VDP's PAL bit: inactive_start 240 in a 224-line
    // frame) is centred too, so dy is signed: the source starts `-dy` rows into the picture and the frame shows as many
    // rows as fit. Anchoring it at the top instead would silently drop the picture's bottom lines.
    const int32_t  dy = (static_cast<int32_t>(frameH) - static_cast<int32_t>(h)) / 2;
    const uint32_t top = dy > 0 ? static_cast<uint32_t>(dy) : 0u;
    const uint32_t srcRow = static_cast<uint32_t>(vdp.border_top) + (dy < 0 ? static_cast<uint32_t>(-dy) : 0u);
    const uint32_t rows = std::min(h, frameH);
    const std::vector<pixel_t>& srcVec = which == FRAMEBUFFER_EVEN ? even_ : odd_;
    const pixel_t* src = srcVec.data();
    std::uint32_t* dst = triple_->writeSlot();
    const std::uint32_t cols = std::min(w, frameW - left);
    // The bars a narrower picture leaves are the console's own BACKDROP, not the shell's zeroed slot: BlastEm paints the
    // border around the active picture, so the pixel just left of BORDER_LEFT is the backdrop colour, and a frame whose
    // every pixel is opaque is what FrameBufferTriple's XRGB8888 contract means (a zero pixel renders as transparent).
    const std::uint32_t borderCol = BORDER_LEFT ? BORDER_LEFT - 1 : 0;
    const pixel_t backdrop = 0xFF000000u | (src[std::uint32_t(vdp.border_top) * LINEBUF_SIZE + borderCol] & 0x00FFFFFFu);
    for (std::uint32_t y = 0; y < frameH; ++y) {
        for (std::uint32_t x = 0; x < frameW; ++x) dst[y * frameW + x] = backdrop;
    }
    for (std::uint32_t y = 0; y < rows; ++y) {
        const pixel_t* row = src + (srcRow + y) * LINEBUF_SIZE + BORDER_LEFT;
        for (std::uint32_t x = 0; x < cols; ++x) dst[(top + y) * frameW + left + x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
    }
    triple_->publish();
    ++published_;
}

}  // namespace rp::blastem
