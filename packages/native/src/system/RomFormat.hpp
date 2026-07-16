#pragma once

#include <cstdint>
#include <vector>

// Detect the platform a given ROM targets — Game Boy, NES, or GBA. Called by
// any caller that needs to gate "is this actually a ROM?" before constructing
// a system (and to route it to the core that runs that platform). Uses magic
// bytes, not the file extension, so a mislabelled .gb that's really a .nes
// still classifies correctly — and a totally unrelated file (a .sh script,
// say) is rejected cleanly instead of being fed to a core as garbage.

enum class RomFormat : std::uint8_t {
    Unknown = 0,  // bytes don't look like any supported ROM
    Gb      = 1,  // Game Boy / Game Boy Color (DMG/CGB)
    Nes     = 2,  // NES (iNES header)
    Gba     = 3,  // Game Boy Advance (Nintendo logo at $0004..$009F)
};

// Returns Nes if `bytes` starts with the iNES magic ("NES\x1A").
// Returns Gba if `bytes` contains the GBA Nintendo logo at offset $0004.
// Returns Gb if `bytes` contains the Game Boy Nintendo logo at offset
// $0104. Returns Unknown otherwise. Empty / short buffers are Unknown.
inline RomFormat detectRomFormat(const std::vector<std::uint8_t>& bytes) {
    // iNES: 'N','E','S',0x1A at offset 0
    if (bytes.size() >= 4 &&
        bytes[0] == 'N' && bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A) {
        return RomFormat::Nes;
    }

    // GBA: Nintendo logo at $0004..$009F. Every licensed GBA cart contains
    // these bytes verbatim — the boot ROM CRC-checks them and refuses to
    // start the cart otherwise. The first 32 bytes are unique enough to use
    // as a signature; the full logo is 156 bytes.
    static constexpr std::uint8_t kGbaLogo[] = {
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21,
        0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
        0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
        0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
    };
    constexpr std::size_t kGbaLogoOffset = 0x04;
    constexpr std::size_t kGbaLogoSize   = sizeof(kGbaLogo);
    if (bytes.size() >= kGbaLogoOffset + kGbaLogoSize) {
        bool match = true;
        for (std::size_t i = 0; i < kGbaLogoSize; ++i) {
            if (bytes[kGbaLogoOffset + i] != kGbaLogo[i]) { match = false; break; }
        }
        if (match) return RomFormat::Gba;
    }

    // Game Boy: Nintendo logo at $0104..$0133. Every licensed (and most
    // unlicensed) GB cartridges contain these bytes verbatim — the boot ROM
    // refuses to start the cart without them, so they're load-bearing for
    // the GB format itself. 48 bytes is enough to be a reliable signature.
    static constexpr std::uint8_t kNintendoLogo[] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
        0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    constexpr std::size_t kLogoOffset = 0x104;
    constexpr std::size_t kLogoSize   = sizeof(kNintendoLogo);
    if (bytes.size() >= kLogoOffset + kLogoSize) {
        bool match = true;
        for (std::size_t i = 0; i < kLogoSize; ++i) {
            if (bytes[kLogoOffset + i] != kNintendoLogo[i]) { match = false; break; }
        }
        if (match) return RomFormat::Gb;
    }

    return RomFormat::Unknown;
}
