#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace rp::blastem {

// The Mega Drive header's magic: "SEGA" at $100, and NOT "SEGA 32X". ONE rule on both sides (packages/blastem/ts/
// consoles.ts's detect is the same four bytes), because two rules that disagree classify a file the shell accepts and
// this gate refuses: a Master System `.bin` has no such string (its header is "TMR SEGA" at the end of a bank) and
// stays Mesen's, a 32X image is a different console this core does not run, and a `.gen`/`.md` file that is neither is
// refused rather than booted as noise.
inline bool isMdRom(std::span<const std::uint8_t> rom) {
    if (rom.size() < 0x108) return false;
    if (std::memcmp(rom.data() + 0x100, "SEGA", 4) != 0) return false;
    return std::memcmp(rom.data() + 0x100, "SEGA 32X", 8) != 0;
}

}  // namespace rp::blastem
