#pragma once

#include "Core/Shared/Interfaces/IRenderingDevice.h"
#include "Core/Shared/RenderedFrame.h"

#include <cstdint>

class FrameBufferTriple;

// IRenderingDevice implementation that copies the most recently decoded video
// frame into the system's lock-free FrameBufferTriple. The triple-buffer is
// owned by MesenSystem; we just hold a non-owning pointer set at activation.
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

private:
    FrameBufferTriple* fb_ = nullptr;
};
