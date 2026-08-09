#pragma once

#include <cstdint>
#include <vector>

// Detect the platform a given ROM targets - Game Boy, NES, GBA, or Sega 8-bit.
// Called by any caller that needs to gate "is this actually a ROM?" before
// constructing a system (and to route it to the core that runs that platform).
// Uses magic bytes, not the file extension, so a mislabelled .gb that's really
// a .nes still classifies correctly - and a totally unrelated file (a .sh
// script, say) is rejected cleanly instead of being fed to a core as garbage.

enum class RomFormat : std::uint8_t {
    Unknown = 0,  // bytes don't look like any supported ROM
    Gb      = 1,  // Game Boy / Game Boy Color (DMG/CGB)
    Nes     = 2,  // NES (iNES header)
    Gba     = 3,  // Game Boy Advance (Nintendo logo at $0004..$009F)
    Sms     = 4,  // Sega 8-bit: Master System OR Game Gear ("TMR SEGA" header)
};

// Note that Sms covers BOTH Sega machines rather than splitting them. Nothing
// here re-decides Master System vs Game Gear: that policy lives in TS
// (platform.ts, which weighs the header's region nibble against the file
// extension), and the answer arrives as spec.platform. Duplicating it here
// would give two sources of truth that could disagree, and the one native
// actually needs - "are these bytes a Sega 8-bit ROM rather than something
// else entirely" - is answered by the magic alone.

// Returns Nes if `bytes` starts with the iNES magic ("NES\x1A").
// Returns Gba if `bytes` contains the GBA Nintendo logo at offset $0004.
// Returns Gb if `bytes` contains the Game Boy Nintendo logo at offset
// $0104. Returns Sms if it carries "TMR SEGA" at a bank-end header offset.
// Returns Unknown otherwise. Empty / short buffers are Unknown.
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

    // Sega 8-bit: "TMR SEGA" at the END of a bank rather than the file start.
    // $7FF0 is the usual spot; $1FF0/$3FF0 are where an 8 KB/16 KB cart puts
    // it. Each is doubled by the +$200 copier-header variant Mesen strips at
    // load (SmsConsole.cpp: `(size % 0x400) == 0x200`) - such a ROM boots
    // fine, so it has to classify too.
    //
    // Unlike the Nintendo logos this magic is NOT required by any boot ROM, so
    // plenty of homebrew omits it. That's why MesenBackend's SMS gate accepts
    // Unknown as well as Sms: see the comment there.
    static constexpr std::uint8_t kSegaMagic[] = { 'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A' };
    constexpr std::size_t kSegaMagicSize   = sizeof(kSegaMagic);
    constexpr std::size_t kSegaCopierSkip  = 0x200;
    constexpr std::size_t kSegaBaseOffsets[] = { 0x7FF0, 0x3FF0, 0x1FF0 };
    for (const std::size_t base : kSegaBaseOffsets) {
        for (const std::size_t off : { base, base + kSegaCopierSkip }) {
            if (bytes.size() < off + kSegaMagicSize) continue;
            bool match = true;
            for (std::size_t i = 0; i < kSegaMagicSize; ++i) {
                if (bytes[off + i] != kSegaMagic[i]) { match = false; break; }
            }
            if (match) return RomFormat::Sms;
        }
    }

    return RomFormat::Unknown;
}
