#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rp::sameboy {

// The Game Boy cartridge logo: the 48 bytes at $0104 that every licensed (and most unlicensed) cartridge carries
// verbatim, because the boot ROM refuses to start without them.
inline constexpr std::size_t kGameBoyLogoOffset = 0x104;
inline constexpr std::uint8_t kGameBoyLogo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

// Whether `bytes` are a Game Boy cartridge: the Nintendo logo where a cartridge puts it ($0104), which every licensed
// (and most unlicensed) cart carries because the boot ROM refuses to start without it. Magic bytes rather than the file
// extension, so a mislabelled .gb that is really a .nes is refused here rather than fed to the core as garbage. The
// embedded mGB passes: it carries a cart's logo (docs/modular-graph-phase-f2-design.md section 3). This check looks for
// the Game Boy's signature alone, so a ROM carrying two consoles' magic is accepted by whichever node the document names
// (the TS side picks the node).
inline bool isGameBoyRom(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kGameBoyLogoOffset + sizeof kGameBoyLogo) return false;
    for (std::size_t i = 0; i < sizeof kGameBoyLogo; ++i)
        if (bytes[kGameBoyLogoOffset + i] != kGameBoyLogo[i]) return false;
    return true;
}

}  // namespace rp::sameboy
