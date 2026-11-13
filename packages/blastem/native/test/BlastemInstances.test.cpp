// Two consoles in one process (design section 2): every arrangement of two or three instances renders and runs what
// each renders ALONE, and the comparison is an instance's own solo run, not the other instance's: a pair that is broken
// the same way on both sides would pass the other comparison.
//
// What "the same" covers: each block's audio AND a hash of the machine state it left (the absolute clock, the 68k's
// registers and PC, work RAM, the Z80's RAM and PC, VRAM, CRAM, VSRAM, the VDP's frame count, the port device's held
// level) AND the frames it published. Audio alone is too weak: the images here that exercise the mirror's globals (a
// 68k-to-VDP DMA reads work RAM, the Z80 runs a program of its own, the VDP writes VRAM through its FIFO) can leave a
// trace in state the speakers do not carry this block.
//
// The arrangements are the ones a host can produce: interleaved on one thread, booted and torn down on separate threads
// at once, and (the engine's migrating contract) an instance whose every block runs on a fresh thread and which is torn
// down on a thread that never ran it. Creation is the arrangement the allocation lock exists for, so the threaded case
// creates its instances INSIDE the threads.
//
// z80.h reaches this file through BlastemCore.hpp (genesis.h includes it under NEW_CORE), so the Z80 case needs no
// include of its own.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"   // the C headers: io.h, vdp.h and Genesis's prototypes
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;
constexpr int           kBlocks = 128;

// The port claimed on every instance: one case drives port 2's levels, and the claim is per instance, so two booted
// the same way must hold the port the same way (the held level is part of the per-block state hash).
std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, SystemId id) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(id, cfg, std::move(rom));
    sys->setPort2Claimed(true);
    sys->onActivate(kRate);
    return sys;
}

// The hash of everything a block leaves behind, chained across blocks. It is what "as if alone" means beyond audio:
// two instances that share a global diverge here even where the picture and the sound happen to agree.
std::uint64_t fnv(std::uint64_t h, const void* bytes, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

std::uint64_t stateHash(rp::blastem::BlastemMdSystem& sys, std::uint64_t chain) {
    genesis_context* gen = sys.core();
    const std::uint64_t absolute = sys.mclkNow();
    chain = fnv(chain, &absolute, sizeof(absolute));
    chain = fnv(chain, gen->m68k->dregs, sizeof(gen->m68k->dregs));
    chain = fnv(chain, gen->m68k->aregs, sizeof(gen->m68k->aregs));
    chain = fnv(chain, &gen->m68k->pc, sizeof(gen->m68k->pc));
    chain = fnv(chain, gen->work_ram, RAM_WORDS * 2);
    chain = fnv(chain, gen->zram, Z80_RAM_BYTES);
    chain = fnv(chain, gen->vdp->vdpmem, VRAM_SIZE);
    chain = fnv(chain, gen->vdp->cram, sizeof(gen->vdp->cram));
    chain = fnv(chain, gen->vdp->vsram, gen->vdp->vsram_size * 2);
    chain = fnv(chain, &gen->vdp->frame, sizeof(gen->vdp->frame));
    chain = fnv(chain, &gen->z80->pc, sizeof(gen->z80->pc));
    chain = fnv(chain, &gen->z80->cycles, sizeof(gen->z80->cycles));
    const std::uint8_t held = sys.portDevice().lastApplied();
    return fnv(chain, &held, sizeof(held));
}

// The port schedule a driven instance gets: a level every 97 host samples, cycling the two-bit counter onto TR and TH.
// It is a function of the ABSOLUTE sample, so a case can give two driven instances different schedules (`bias`) and a
// misrouted read then shows up as one instance's answer in the other's state.
std::uint8_t levelFor(std::uint64_t sample, std::uint64_t bias) {
    const std::uint8_t counter = std::uint8_t(((sample + bias) / 97) & 3);
    return std::uint8_t(((counter & 1) << 5) | ((counter & 2) << 5));
}

// Everything one run produces, block by block.
struct Trace {
    std::vector<float>    audio;
    std::vector<std::uint64_t> state;
    std::uint64_t         frames = 0;
};

struct Inst {
    std::unique_ptr<rp::blastem::BlastemMdSystem> sys;
    std::vector<float>                            l, r;
    Trace                                         trace;
    std::uint64_t                                 sample = 0;
    bool                                          port   = false;  // drive port 2 from levelFor()
    std::uint64_t                                 bias   = 0;
    std::uint64_t                                 chain  = 0xCBF29CE484222325ull;

    void bootWith(std::vector<std::uint8_t> rom, SystemId id) {
        sys = boot(std::move(rom), id);
        l.assign(kFrames, 0.0f);
        r.assign(kFrames, 0.0f);
    }

    void block() {
        float* lanes[2] = {l.data(), r.data()};
        AudioBlockInfo info{};
        info.frames = kFrames;
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys->prepareForBlock(info);
        if (port) {
            for (std::uint32_t f = 0; f < kFrames; ++f) {
                const std::uint64_t n = sample + f;
                if (n % 97 == 0) {
                    const std::uint8_t level = levelFor(n, bias);
                    sys->pushCoreBytes(f, &level, 1);
                }
            }
        }
        while (sys->stepIfBelowTarget(kFrames)) {}
        sys->finishBlock(info, lanes, 2);
        sample += kFrames;
        trace.audio.insert(trace.audio.end(), l.begin(), l.end());
        trace.audio.insert(trace.audio.end(), r.begin(), r.end());
        chain = stateHash(*sys, chain);
        trace.state.push_back(chain);
        trace.frames = sys->video().framesPublished();
    }

    void run(int blocks) {
        for (int i = 0; i < blocks; ++i) block();
    }

    void destroy() { sys.reset(); }
};

struct RomSpec {
    std::vector<std::uint8_t> rom;
    bool                      port = false;
    std::uint64_t             bias = 0;
};

Trace solo(const RomSpec& spec, int blocks) {
    Inst in;
    in.port = spec.port;
    in.bias = spec.bias;
    in.bootWith(spec.rom, 1);
    in.run(blocks);
    Trace trace = in.trace;
    in.destroy();
    return trace;
}

// The one comparison every arrangement makes: this instance's block-by-block trace against the trace the same image
// and the same port schedule produce alone.
void checkAgainstSolo(const Trace& got, const Trace& want) {
    CHECK(got.audio == want.audio);
    CHECK(got.state == want.state);
    CHECK(got.frames == want.frames);
}

RomSpec spec(std::vector<std::uint8_t> rom, bool port = false, std::uint64_t bias = 0) {
    RomSpec s;
    s.rom  = std::move(rom);
    s.port = port;
    s.bias = bias;
    return s;
}

}  // namespace

TEST_CASE("two instances interleaved on one thread render what each renders alone", "[blastem][instances]") {
    // A running a 68k-to-VDP DMA from its VINT handler against one driving port 2: the DMA reads the console's own work
    // RAM, and the port read has to answer for the console it came out of, not for whichever instance ran last.
    const RomSpec a = spec(rp::blastem::test::vintDmaRom());
    const RomSpec b = spec(rp::blastem::test::portLevelRom(), true, 1);
    const Trace   aloneA = solo(a, kBlocks);
    const Trace   aloneB = solo(b, kBlocks);

    Inst ia;
    Inst ib;
    ia.bootWith(a.rom, 1);
    ib.port = b.port;
    ib.bias = b.bias;
    ib.bootWith(b.rom, 2);
    for (int i = 0; i < kBlocks; ++i) {
        ia.block();
        ib.block();
    }
    checkAgainstSolo(ia.trace, aloneA);
    checkAgainstSolo(ib.trace, aloneB);
    // Not vacuously: the two images are different machines, and the driven one's levels reached its pins (a schedule
    // that never landed would compare idle against idle).
    CHECK(ia.trace.audio != ib.trace.audio);
    CHECK(ib.sys->levelsAccepted() > 0);
    ia.destroy();
    ib.destroy();
}

TEST_CASE("two instances on two threads render what each renders alone", "[blastem][instances]") {
    // Booted, run and torn down each on its own thread, at the same time: that is what the allocation lock is for, and
    // without it BlastEm's own allocation statics (the m68k sync handoff, the YM and VDP table inits, the systems
    // config) are written by two consoles at once. TWO of the three threads run the VINT DMA, because that is the pair
    // with the most process-wide state behind it: the DMA reads work RAM and the VDP's FIFO writes the event log's VRAM
    // delta state, and the third thread runs the Z80's own program beside them.
    const RomSpec dma = spec(rp::blastem::test::vintDmaRom());
    const RomSpec z80 = spec(rp::blastem::test::z80TimingRom());
    const Trace   aloneDma = solo(dma, kBlocks);
    const Trace   aloneZ80 = solo(z80, kBlocks);

    Trace gotA;
    Trace gotB;
    Trace gotZ;
    auto  runOnThread = [&](const RomSpec& rom, SystemId id, Trace& into) {
        Inst in;
        in.bootWith(rom.rom, id);
        in.run(kBlocks);
        into = in.trace;
        in.destroy();
    };
    std::thread first([&] { runOnThread(dma, 1, gotA); });
    std::thread second([&] { runOnThread(dma, 2, gotB); });
    std::thread third([&] { runOnThread(z80, 3, gotZ); });
    first.join();
    second.join();
    third.join();
    checkAgainstSolo(gotA, aloneDma);
    checkAgainstSolo(gotB, aloneDma);
    checkAgainstSolo(gotZ, aloneZ80);
}

TEST_CASE("an instance whose every block runs on a fresh thread is still alone", "[blastem][instances]") {
    // The engine's contract for a system that migrates: one call at a time, on whatever thread calls it. Each block of
    // each instance runs on a thread of its own, the three instances' blocks at the same time, and teardown happens on
    // a thread that never ran any of them.
    std::vector<RomSpec> specs = {spec(rp::blastem::test::hvPsgRom()), spec(rp::blastem::test::z80TimingRom()),
                                  spec(rp::blastem::test::portLevelRom(), true, 2)};
    std::vector<Trace>   alone;
    for (const RomSpec& s : specs) alone.push_back(solo(s, kBlocks));

    std::vector<Inst> live(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        live[i].port = specs[i].port;
        live[i].bias = specs[i].bias;
        live[i].bootWith(specs[i].rom, SystemId(i + 1));
    }
    for (int block = 0; block < kBlocks; ++block) {
        std::vector<std::thread> threads;
        for (Inst& in : live) threads.emplace_back([&in] { in.block(); });
        for (std::thread& t : threads) t.join();
    }
    for (std::size_t i = 0; i < live.size(); ++i) checkAgainstSolo(live[i].trace, alone[i]);
    std::thread([&live] {
        for (Inst& in : live) in.destroy();
    }).join();
}

TEST_CASE("instances booted and destroyed in a loop beside a running one stay alone", "[blastem][instances]") {
    // The churn a host does when a user reloads a ROM: one instance keeps running on its own thread while another thread
    // boots, runs and destroys instances over and over. Allocation and teardown are what the lock covers, so this is the
    // arrangement where a missing lock is loudest (the review's own harness died in it within a second), and every
    // churned instance must produce the same first blocks as that image does alone.
    constexpr int kChurnBlocks = kBlocks / 2;
    const RomSpec dma = spec(rp::blastem::test::vintDmaRom());
    const RomSpec z80 = spec(rp::blastem::test::z80TimingRom());
    const Trace   aloneDma = solo(dma, kChurnBlocks);
    const Trace   aloneZ80 = solo(z80, 8);

    std::atomic<bool> finished{false};
    Trace             ranThrough;
    std::thread runner([&] {
        Inst in;
        in.bootWith(dma.rom, 1);
        in.run(kChurnBlocks);
        ranThrough = in.trace;
        in.destroy();
        finished = true;
    });
    std::vector<Trace> churned;
    std::thread        churner([&] {
        for (int i = 0; !finished.load() && churned.size() < 64; ++i) {
            Inst in;
            in.bootWith(z80.rom, SystemId(100 + i));
            in.run(8);
            churned.push_back(in.trace);
            in.destroy();
        }
    });
    runner.join();
    churner.join();
    checkAgainstSolo(ranThrough, aloneDma);
    REQUIRE(!churned.empty());
    // Every churned trace is compared, but folded into TWO assertions: how many the churner managed is how much of the
    // runner it overlapped, so an assertion per iteration would make the binary's assertion count differ run to run.
    bool sameState = true;
    bool sameAudio = true;
    for (const Trace& trace : churned) {
        if (trace.state != aloneZ80.state) sameState = false;
        if (trace.audio != aloneZ80.audio) sameAudio = false;
    }
    CHECK(sameState);
    CHECK(sameAudio);
}

TEST_CASE("a soft reset delays by the same amount on two consoles that ran the same program", "[blastem][instances]") {
    // The reset's delay used to come from rand(), the process-wide C library generator: each console got the next
    // number in one shared sequence, so a reset depended on how many draws every other console (and the host) had
    // made. Two consoles that have run the same program must reset at the same phase of their own frame.
    Inst ia;
    Inst ib;
    ia.bootWith(rp::blastem::test::hvPsgRom(), 1);
    ib.bootWith(rp::blastem::test::hvPsgRom(), 2);
    ia.run(4);
    ib.run(4);
    // Same program, same clock: the 68k's own count is the same on both, so any difference in the reset is the delay.
    REQUIRE(ia.sys->core()->m68k->cycles == ib.sys->core()->m68k->cycles);
    system_header* ha = &ia.sys->core()->header;
    system_header* hb = &ib.sys->core()->header;
    ha->soft_reset(ha);
    hb->soft_reset(hb);
    CHECK(ia.sys->core()->reset_cycle == ib.sys->core()->reset_cycle);
    // And the delay is a phase within the frame that console is in, not a value off the end of it: 262 lines is an
    // NTSC frame's total, blanking included.
    const std::uint32_t delay = ia.sys->core()->reset_cycle - ia.sys->core()->m68k->cycles;
    CHECK(delay > 0);
    CHECK(delay <= MCLKS_LINE * 262);
}

TEST_CASE("each console's border-busting line is its own storage", "[blastem][instances]") {
    // vdp_h40, vdp_h32 and vdp_h32_mode4 write a line to a fallback buffer when there is no output buffer (a mode
    // change during the active display, which no image here performs). That buffer was one file-scope array every
    // console in the process wrote through, so two consoles doing it at once raced in it; it is a line in each
    // console's own VDP now.
    auto a = boot(rp::blastem::test::hvPsgRom(), 1);
    auto b = boot(rp::blastem::test::hvPsgRom(), 2);
    CHECK(a->core()->vdp->dummy_line != b->core()->vdp->dummy_line);
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
