// Two consoles in one process (design section 2): interleaved on one thread and run on two, each bit-identical to a
// solo render. The DMA case is the one that caught a real global: a second instance used to read its VDP DMA out of
// the first instance's work RAM.
//
// z80.h reaches this file through BlastemCore.hpp (genesis.h includes it under NEW_CORE), so the Z80 case needs no
// include of its own.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"   // the C headers: io.h, vdp.h and Genesis's prototypes
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, SystemId id) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(id, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

std::vector<float> solo(std::vector<std::uint8_t> rom, int blocks) {
    auto sys = boot(std::move(rom), 1);
    std::vector<float> l(kFrames), r(kFrames), out;
    for (int i = 0; i < blocks; ++i) {
        runBlock(*sys, l, r);
        out.insert(out.end(), l.begin(), l.end());
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

}  // namespace

TEST_CASE("two instances interleaved on one thread render what each renders alone", "[blastem][instances]") {
    const std::vector<float> alone = solo(rp::blastem::test::fmPsgRom(), 40);
    auto a = boot(rp::blastem::test::fmPsgRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);  // a different ROM: its DMA and ROM reads would cross
    std::vector<float> la(kFrames), ra(kFrames), lb(kFrames), rb(kFrames), out;
    for (int i = 0; i < 40; ++i) {
        runBlock(*a, la, ra);
        runBlock(*b, lb, rb);
        out.insert(out.end(), la.begin(), la.end());
        out.insert(out.end(), ra.begin(), ra.end());
    }
    CHECK(out == alone);
}

TEST_CASE("two instances on two threads render what each renders alone", "[blastem][instances]") {
    const std::vector<float> alone = solo(rp::blastem::test::fmPsgRom(), 40);
    const std::vector<float> aloneB = solo(rp::blastem::test::psgOnlyRom(), 40);
    auto a = boot(rp::blastem::test::fmPsgRom(), 1);
    auto b = boot(rp::blastem::test::psgOnlyRom(), 2);  // a different ROM, and one that also makes sound
    std::vector<float> outA, outB;
    // Creation is serialised (the host builds cores on one thread); only the blocks run concurrently, which is the
    // engine's contract for a system that migrates between threads.
    std::thread first([&] {
        std::vector<float> l(kFrames), r(kFrames);
        for (int i = 0; i < 40; ++i) {
            runBlock(*a, l, r);
            outA.insert(outA.end(), l.begin(), l.end());
            outA.insert(outA.end(), r.begin(), r.end());
        }
    });
    std::thread second([&] {
        std::vector<float> l(kFrames), r(kFrames);
        for (int i = 0; i < 40; ++i) {
            runBlock(*b, l, r);
            outB.insert(outB.end(), l.begin(), l.end());
            outB.insert(outB.end(), r.begin(), r.end());
        }
    });
    first.join();
    second.join();
    CHECK(outA == alone);
    // B against ITS OWN solo render, which is what makes the pair a test: a comparison against A's render would pass
    // with both instances broken in the same way, and a cross-read (A's globals reaching B's audio) shows up here as
    // B's tone in A's lanes or A's in B's.
    CHECK(outB == aloneB);
}

TEST_CASE("a console's DMA reads its own work RAM", "[blastem][instances]") {
    // read_dma_value and vdp_dma_started reached their console through the process global current_system, which the
    // shim left NULL: with two instances the read belongs to whichever console was allocated last (or to nothing at
    // all, which dereferences NULL here). Each instance's RAM holds a different word, so the read cannot be right twice.
    // The address is a WORD address: read_dma_value multiplies by two (genesis.c), so reading 68k $FF0000 is
    // 0xFF0000 >> 1. The RAM chunk's mask is 0xFFFF, so that byte address lands on work_ram's word 0.
    auto a = boot(rp::blastem::test::portPollRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);
    a->core()->work_ram[0] = 0xAAAA;
    b->core()->work_ram[0] = 0x5555;
    CHECK(read_dma_value(a->core()->vdp, 0xFF0000 >> 1) == 0xAAAA);
    CHECK(read_dma_value(b->core()->vdp, 0xFF0000 >> 1) == 0x5555);
}

TEST_CASE("a console's Z80 re-resolves through its own RAM", "[blastem][instances]") {
    // z80_map was a function-local static whose [0].buffer every allocation overwrote (and init_z80_opts keeps it), so
    // the FIRST console's Z80 re-resolved through the LAST one's zram on every invalidate: a savestate load in one
    // console pointed the other's Z80 at freed memory once that one was gone. fastread/fastwrite are what the
    // re-resolution writes (z80_util.c), and [0] covers Z80 addresses 0..1023.
    auto a = boot(rp::blastem::test::portPollRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);  // allocated LAST: the instance the static map would point at
    z80_invalidate_code_range(a->core()->z80, 0, 0x4000);  // what a state load and a Z80 code write do
    CHECK(a->core()->z80->fastwrite[0] == a->core()->zram);
    CHECK(b->core()->z80->fastwrite[0] == b->core()->zram);
    CHECK(a->core()->zram != b->core()->zram);
}

TEST_CASE("a soft reset delays by the same amount on two consoles that ran the same program", "[blastem][instances]") {
    // The reset's delay used to come from rand(), the process-wide C library generator: each console got the next
    // number in one shared sequence, so a reset depended on how many draws every other console (and the host) had made.
    // Two consoles that have run the same program must reset at the same phase of their own frame.
    auto a = boot(rp::blastem::test::hvPsgRom(), 1);
    auto b = boot(rp::blastem::test::hvPsgRom(), 2);
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) {
        runBlock(*a, l, r);
        runBlock(*b, l, r);
    }
    // Same program, same clock: the 68k's own count is the same on both, so any difference in the reset is the delay.
    REQUIRE(a->core()->m68k->cycles == b->core()->m68k->cycles);
    system_header* ha = &a->core()->header;
    system_header* hb = &b->core()->header;
    ha->soft_reset(ha);
    hb->soft_reset(hb);
    CHECK(a->core()->reset_cycle == b->core()->reset_cycle);
    // And the delay is a phase within the frame that console is in, not a value off the end of it: 262 lines is an
    // NTSC frame's total, blanking included.
    const std::uint32_t delay = a->core()->reset_cycle - a->core()->m68k->cycles;
    CHECK(delay > 0);
    CHECK(delay <= MCLKS_LINE * 262);
}

TEST_CASE("each console's border-busting line is its own storage", "[blastem][instances]") {
    // vdp_h40, vdp_h32 and vdp_h32_mode4 write a line to a fallback buffer when there is no output buffer (a mode change
    // during the active display, which no image here performs). That buffer was one file-scope array every console in
    // the process wrote through, so two consoles doing it at once raced in it; it is a line in each console's own VDP.
    auto a = boot(rp::blastem::test::hvPsgRom(), 1);
    auto b = boot(rp::blastem::test::hvPsgRom(), 2);
    CHECK(a->core()->vdp->dummy_line != b->core()->vdp->dummy_line);
}
