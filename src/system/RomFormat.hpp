#pragma once

#include <cstdint>
#include <vector>

// Detect the emulator backend a given ROM file should run on. Called by
// PluginJsBridge::buildSystemFromPath (and any caller that needs to gate
// "is this actually a ROM?" before constructing a system). Uses magic bytes,
// not the file extension, so a mislabelled .gb that's really a .nes still
// picks the right backend — and a totally unrelated file (a .sh script,
// say) is rejected cleanly instead of being fed to SameBoy as garbage.

enum class RomFormat : std::uint8_t {
    Unknown = 0,  // bytes don't look like any supported ROM
    SameBoy = 1,  // Game Boy / Game Boy Color (DMG/CGB)
    Mesen   = 2,  // NES (iNES header)
};

// Returns Mesen if `bytes` starts with the iNES magic ("NES\x1A").
// Returns SameBoy if `bytes` contains the Nintendo boot-logo bytes at the
// Game Boy cartridge header location ($0104..$0133).
// Returns Unknown otherwise. Empty / short buffers are Unknown.
inline RomFormat detectRomFormat(const std::vector<std::uint8_t>& bytes) {
    // iNES: 'N','E','S',0x1A at offset 0
    if (bytes.size() >= 4 &&
        bytes[0] == 'N' && bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A) {
        return RomFormat::Mesen;
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
        if (match) return RomFormat::SameBoy;
    }

    return RomFormat::Unknown;
}
