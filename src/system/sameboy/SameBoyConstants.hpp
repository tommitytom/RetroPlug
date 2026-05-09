#pragma once

#include <cstdint>

namespace sameboy {
    constexpr std::uint32_t kPixelWidth      = 160;
    constexpr std::uint32_t kPixelHeight     = 144;
    constexpr std::uint32_t kPixelCount      = kPixelWidth * kPixelHeight;
    constexpr std::uint32_t kFrameByteSize   = kPixelCount * 4;
}
