#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "native/core/img/png/lodepng.h"

#include "system/SystemBase.hpp"
#include "transport/FrameBufferTriple.hpp"

namespace rpcli {

// Write a system's most-recently-published framebuffer to `outPath` as an
// RGB24 PNG. Returns false when no frame has been published yet or the encode
// fails. Shared by the --script screenshot events and the --test harness's
// emu.screenshot().
//
// FrameBufferTriple stores XRGB8888 (little-endian B,G,R,X); transcode to
// RGB24 to match lodepng_encode24_file (same logic as src/PluginUI.cpp).
inline bool writeFramebufferPng(SystemBase& sys, const std::string& outPath) {
    FrameBufferTriple* fb = sys.framebuffer();
    if (!fb) return false;

    const std::uint32_t w = fb->width();
    const std::uint32_t h = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(w) * h;

    std::vector<std::uint32_t> xrgb(pixels);
    if (!fb->readInto(xrgb.data(), static_cast<std::uint32_t>(pixels)))
        return false;

    std::vector<unsigned char> rgb(pixels * 3);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(xrgb.data());
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = src[i * 4 + 2]; // R
        rgb[i * 3 + 1] = src[i * 4 + 1]; // G
        rgb[i * 3 + 2] = src[i * 4 + 0]; // B
    }

    return lodepng_encode24_file(outPath.c_str(), rgb.data(), w, h) == 0;
}

} // namespace rpcli
