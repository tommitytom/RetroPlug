#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rp::mesen {

// What Mesen's three consoles gate a ROM on (docs/modular-graph-phase-f2-design.md section 3): magic bytes, not the
// file extension, so a mislabelled ROM is refused by the console that would run it wrongly rather than fed to a core as
// garbage.

// The Game Boy cartridge logo, copied rather than shared: a console package keeps the magic bytes it checks, and mesen
// may not depend on sameboy.
inline constexpr std::size_t kGameBoyLogoOffset = 0x104;
inline constexpr std::uint8_t kGameBoyLogo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

// Whether `bytes` carry the logo where a cartridge puts it, which no Mesen console runs. A buffer too short to hold it
// carries none.
inline bool carriesGameBoyLogo(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kGameBoyLogoOffset + sizeof kGameBoyLogo) return false;
    for (std::size_t i = 0; i < sizeof kGameBoyLogo; ++i)
        if (bytes[kGameBoyLogoOffset + i] != kGameBoyLogo[i]) return false;
    return true;
}

// iNES: 'N','E','S',0x1A at offset 0.
inline bool isNesRom(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 4 && bytes[0] == 'N' && bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A;
}

// The GBA Nintendo logo at $0004..$009F: every licensed cart contains these bytes verbatim, since the boot ROM
// CRC-checks them and refuses to start the cart otherwise. The first 32 are unique enough to use as a signature; the
// full logo is 156 bytes.
inline bool isGbaRom(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t kGbaLogo[] = {
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
        0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21, 0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
    };
    constexpr std::size_t kOffset = 0x04;
    if (bytes.size() < kOffset + sizeof kGbaLogo) return false;
    for (std::size_t i = 0; i < sizeof kGbaLogo; ++i)
        if (bytes[kOffset + i] != kGbaLogo[i]) return false;
    return true;
}

// Sega 8-bit: "TMR SEGA" at the END of a bank rather than at the file start. $7FF0 is the usual spot; $1FF0/$3FF0 are
// where an 8 KB/16 KB cart puts it. Each is doubled by the +$200 copier-header variant Mesen strips at load
// (SmsConsole.cpp: `(size % 0x400) == 0x200`), which boots fine, so it classifies too. This covers BOTH Sega machines:
// nothing here re-decides Master System vs Game Gear, which is TS's policy (platform.ts weighs the header's region
// nibble against the file extension). Unlike the Nintendo logos this magic is required by no boot ROM, so plenty of
// homebrew omits it, which is why buildSmsCore accepts a ROM that carries none of them.
inline bool isSegaRom(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t kSegaMagic[] = {'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A'};
    constexpr std::size_t kCopierSkip = 0x200;
    constexpr std::size_t kBases[] = {0x7FF0, 0x3FF0, 0x1FF0};
    for (const std::size_t base : kBases) {
        for (const std::size_t off : {base, base + kCopierSkip}) {
            if (bytes.size() < off + sizeof kSegaMagic) continue;
            bool match = true;
            for (std::size_t i = 0; i < sizeof kSegaMagic; ++i)
                if (bytes[off + i] != kSegaMagic[i]) {
                    match = false;
                    break;
                }
            if (match) return true;
        }
    }
    return false;
}

}  // namespace rp::mesen
