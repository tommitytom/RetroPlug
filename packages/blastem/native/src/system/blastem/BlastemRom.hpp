#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "system/blastem/BlastemCore.hpp"  // aligned_calloc

namespace rp::blastem {

// The Mega Drive header's magic: "SEGA" at $100, and NOT "SEGA 32X". ONE rule, and the shell's classifier carries the
// same four bytes, because two rules that disagree classify a file the shell accepts and this gate refuses: a Master
// System `.bin` has no such string (its header is "TMR SEGA" at the end of a bank) and stays Mesen's, a 32X image is a
// different console this core does not run, and a `.gen`/`.md` file that is neither is refused rather than booted as
// noise.
inline bool isMdRom(std::span<const std::uint8_t> rom) {
    if (rom.size() < 0x108) return false;
    if (std::memcmp(rom.data() + 0x100, "SEGA", 4) != 0) return false;
    return std::memcmp(rom.data() + 0x100, "SEGA 32X", 8) != 0;
}

// The cart as BlastEm wants it, which is BlastEm's own allocation: alloc_config_genesis takes the pointer, byteswaps it
// in place over nearest_pow2(size) bytes (shared_init_gen), and free_genesis releases it with aligned_free, which finds
// the allocation by reading the byte BEFORE the pointer (buf -= buf[-1], util.c:60). A std::vector's buffer is not that
// allocation, and handing it over is a double free at teardown. So the image is copied into aligned_calloc's buffer,
// padded to the power of two BlastEm byteswaps over, and BlastEm owns it from boot to free.
//
// The alignment is 16, BlastEm's own (romdb.c:274), and never more: aligned_calloc keeps it in ONE byte
// (`ret[-1] = (uint8_t)align`, util.c:46), so 4096 truncates to 0 and aligned_free then frees a misaligned pointer.
inline void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 16);
    if (!copy) return nullptr;
    std::memcpy(copy, rom.data(), rom.size());
    return copy;
}

}  // namespace rp::blastem
