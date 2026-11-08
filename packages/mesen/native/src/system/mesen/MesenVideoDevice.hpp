#pragma once

#include "Core/Shared/Interfaces/IRenderingDevice.h"
#include "Core/Shared/RenderedFrame.h"

#include <atomic>
#include <cstdint>

class FrameBufferTriple;

// IRenderingDevice implementation that copies the most recently decoded video
// frame into the system's lock-free FrameBufferTriple. The triple-buffer is
// owned by MesenNesSystem; we just hold a non-owning pointer set at activation.
//
// Mesen emits 0xAARRGGBB pixels (32-bit ARGB packed); FrameBufferTriple stores
// uint32_t in LVGL-native byte order (B,G,R,X for LV_COLOR_DEPTH=32, written as
// 0xFFRRGGBB in host-endian terms — matches SameBoy's `rgbEncode`). The two
// representations are byte-equivalent on little-endian; convert per-pixel for
// portability and to drop Mesen's alpha byte.
class MesenVideoDevice final : public IRenderingDevice {
public:
    void setFramebuffer(FrameBufferTriple* fb) { fb_ = fb; }

    void UpdateFrame(RenderedFrame& frame) override;

    void ClearFrame() override {}

    // Mesen has its own render thread surface; we don't drive it.
    void Render(RenderSurfaceInfo&, RenderSurfaceInfo&) override {}
    void Reset() override {}
    void SetExclusiveFullscreenMode(bool, void*) override {}

    // Dimensions of the last frame Mesen handed us, BEFORE the min-clamp in
    // UpdateFrame. Diagnostic only: the clamp means a core whose overscan
    // config disagrees with the triple's size produces a silently wrong picture
    // rather than an error, so this is the only way to see the disagreement.
    // Zero until the first frame arrives.
    //
    // Relaxed atomics because UpdateFrame runs on whichever thread is driving
    // emulation while these accessors are public to anyone. Relaxed is enough:
    // the two values are independent diagnostics that order nothing else, and a
    // reader that catches a half-updated pair sees a stale value, not garbage.
    std::uint32_t lastFrameWidth()  const { return lastW_.load(std::memory_order_relaxed); }
    std::uint32_t lastFrameHeight() const { return lastH_.load(std::memory_order_relaxed); }

private:
    FrameBufferTriple*         fb_ = nullptr;
    std::atomic<std::uint32_t> lastW_{0};
    std::atomic<std::uint32_t> lastH_{0};
};
