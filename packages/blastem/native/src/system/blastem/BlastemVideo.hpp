#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/blastem/BlastemCore.hpp"  // pixel_t, FRAMEBUFFER_*, LINEBUF_SIZE, BORDER_LEFT, vdp_context

// The shell's frame triple is a GLOBAL class (transport/FrameBufferTriple.hpp), so the forward declaration has to be at
// global scope too: one declared inside rp::blastem names a different, unrelated type, and the sink's unique_ptr and
// BlastemMdSystem::framebuffer()'s override would both bind to it instead of the real one.
class FrameBufferTriple;

namespace rp::blastem {

// BlastEm's framebuffer seam: two buffers the VDP draws into (FRAMEBUFFER_ODD / FRAMEBUFFER_EVEN alternate per frame),
// and a publish that places the active picture inside the FrameBufferTriple the engine reads. BlastEm keeps its own
// bordered frame (LINEBUF_SIZE wide, the border rows around the picture), so the crop is the whole conversion.
class VideoSink {
public:
    VideoSink() = default;
    ~VideoSink();  // out of line: triple_ is an incomplete type here

    // [control, before the core allocates] Allocate the two buffers the VDP draws into. This has to happen BEFORE
    // alloc_config_genesis, not after: the VDP asks render_get_framebuffer for a buffer at init (vdp.c:524) and
    // dereferences it immediately (context->output = fb + pitch * border_top, vdp.c:529-530).
    void prepareBuffers();

    // [control, boot only, after the core exists] Allocate the frame once: 320 wide (the widest mode) by `regionLines`
    // (224 NTSC, 240 PAL), from the region the core just chose. The shell's frame slot is sized at claim and never
    // resized, so this is the only place a size is decided; publish() returns early until it has run.
    void prepareFrame(std::uint32_t regionLines);

    // const because coreFrameWidth()/coreFrameHeight() are, and they read the triple's size through it: it hands out
    // the frame to WRITE (the engine's slot), so the sink's own constness says nothing about the frame's.
    FrameBufferTriple* triple() const { return triple_.get(); }
    std::uint32_t      width() const;
    std::uint32_t      height() const;
    std::uint64_t      framesPublished() const { return published_; }

    // The buffer `which` names, at BlastEm's pitch. Stable until the instance is destroyed: the VDP asks for one at
    // init and again after every published frame. Null when prepareBuffers() has not run, which lets the caller fall
    // back to its own landing pad rather than hand the VDP a null to draw through; and null for BlastEm's debug indices
    // (>= FRAMEBUFFER_USER_START), which are 1024-wide rows nothing in this build enables, so that a caller cannot
    // mistake the odd buffer for one of them.
    pixel_t* buffer(std::uint8_t which, int* pitch);
    // `width` is BlastEm's frame-end hint (vdp.c passes the line width it just finished) and is ignored: the pitch is
    // always LINEBUF_SIZE, which buffer() sets, and the picture's own width comes from the VDP's mode register.
    void publish(std::uint8_t which, int width, const vdp_context& vdp);

private:
    // 512 rows, the size the VDP's own headless allocation uses (vdp.c:525): the bordered frame's picture plus border
    // is under 280 rows, but the VDP writes at row `output_line`, and 512 is the bound its own code assumes.
    static constexpr std::uint32_t kRows = 512;
    std::vector<pixel_t>           odd_;
    std::vector<pixel_t>           even_;
    std::unique_ptr<FrameBufferTriple> triple_;
    std::uint64_t                  published_ = 0;
};

}  // namespace rp::blastem
