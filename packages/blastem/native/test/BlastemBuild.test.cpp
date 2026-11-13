// The library's link proof: the whole Mega Drive file set, the generated 68k and Z80 interpreters, the shim and the
// ROM database compile and link, and a context builds from a real image and frees. Everything else in this package
// builds on this case, and it is what a new CI platform fails first.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/blastem/BlastemCore.hpp"

namespace {

// BlastEm owns its cart: alloc_config_genesis takes the pointer, byteswaps it in place over nearest_pow2(size) bytes
// and free_genesis releases it with aligned_free. So the buffer must be aligned_calloc's, and its alignment must fit in
// the ONE byte aligned_free reads before the pointer (`ret[-1] = (uint8_t)align` in aligned_calloc): 4096 truncates to
// 0 and frees a misaligned pointer. 16 is BlastEm's own alignment (romdb.c:274).
void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 16);
    if (!copy) return nullptr;
    std::memcpy(copy, rom.data(), rom.size());
    return copy;
}

// The smallest thing BlastEm will boot: 0x200 bytes with a valid "SEGA" header at $100, a stack pointer and a reset
// vector at $000. Nothing executes this image (the reset vector, $000200, is one past its end), so the rest is zero.
std::vector<std::uint8_t> minimalRom() {
    std::vector<std::uint8_t> rom(0x200, 0x00);
    const std::uint8_t vectors[8] = {0x00, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x02, 0x00};
    std::memcpy(rom.data(), vectors, sizeof(vectors));
    std::memcpy(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    return rom;
}

}  // namespace

TEST_CASE("the BlastEm library links and a Mega Drive context builds", "[blastem][build]") {
    // The shim's own symbol, named before anything else: this reference is what pulls BlastemShim.cpp.o out of the
    // package's archive, so the core's references reach the shim (see the test target's comment).
    REQUIRE(rp::blastem::currentInstance == nullptr);
    const std::vector<std::uint8_t> rom = minimalRom();
    genesis_context* gen = alloc_config_genesis(blastemRomCopy(rom), static_cast<std::uint32_t>(rom.size()), nullptr,
                                                0, 0, REGION_U);
    REQUIRE(gen != nullptr);
    CHECK(gen->master_clock == 53693175u);
    CHECK(gen->work_ram != nullptr);
    CHECK(gen->vdp != nullptr);
    m68k_reset(gen->m68k);
    // free_genesis is static in genesis.c: the header's own free is the door. A misaligned cart copy aborts here
    // ("double free or corruption"), which is exactly what this case exists to catch.
    gen->header.free_context(&gen->header);
}
