// Guards the Master System / Game Gear backend (MesenSmsSystem) at the point where it is still
// native-only: the system is not reachable from MesenBackend yet, so constructing it directly is
// the only way to exercise it. Four properties, each of which fails SILENTLY without an assertion:
//
//   1. Boot. Mesen picks the machine from the ROM file EXTENSION (SmsConsole has no signature), so
//      a staging bug boots the wrong console rather than failing.
//   2. Visible geometry. Mesen's per-console overscan defaults live in the .NET UI this build does
//      not compile, so they arrive as {0,0,0,0} and every model emits 256x240. MesenVideoDevice
//      min-clamps into the framebuffer, so a mismatch is a cropped picture, not an error.
//   3. Non-silent PSG. SmsConfig::ChannelVolumes defaults to {0,0,0,0} and SmsPsg multiplies by
//      volumes[i]/100, producing a CORRECT SAMPLE COUNT of pure zeros. A boot test passes while
//      silent; only an amplitude assertion catches it.
//   4. Teardown. SmsFmAudio unregisters from a SoundMixer that Emulator has already destroyed, so
//      construct/destruct segfaults unless onDeactivate calls Emulator::Stop first.
//
//      This guard is PROBABILISTIC and weak on its own. Measured against a deliberately broken
//      build (Stop removed): 1 crash in 5 runs, and raising the loop from 40 to 200 cycles did not
//      improve that - the crash tracks process heap/ASLR state, not iteration count. Kept because
//      it is nearly free, never false-positives, and runs on every CI pass. The deterministic
//      detector is AddressSanitizer, which DOES name this: the root CMakeLists applies
//      -fsanitize before the dep subdirectories, so deps/mesen is instrumented in build-asan/
//      (unlike the default build/, where libmesen.a is uninstrumented and ASan is genuinely blind).
//
// Not covered here, deliberately: the step loop's timing behaviour (block residue bound, gate-metric
// fidelity, cadence invariance). Those need determinism pinning and a cadence knob, and they land
// with the sync work.
//
// Run via `pnpm test:plugin`.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "transport/FrameBufferTriple.hpp"

#ifndef RP_SMS_ROM_PATH
#error "RP_SMS_ROM_PATH must be defined (path to resources/roms/smsggdj_v0_45.sms)"
#endif
#ifndef RP_GG_ROM_PATH
#error "RP_GG_ROM_PATH must be defined (path to resources/roms/smsggdj_v0_45.gg)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 512;
// smsggdj clears its work RAM, sets up the VDP and draws its UI before it ever touches the PSG, so
// give it a real boot before asking about audio. ~1.3 s.
constexpr int           kBootBlocks = 120;

std::vector<std::uint8_t> readRom(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    INFO("ROM path: " << path);
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(n > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
    return bytes;
}

std::unique_ptr<MesenSmsSystem> build(bool gameGear, SystemId id = 1) {
    const char* path = gameGear ? RP_GG_ROM_PATH : RP_SMS_ROM_PATH;
    MesenSmsConfig cfg;
    cfg.gameGear = gameGear;
    cfg.romPath  = path;
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, readRom(path));
    sys->onActivate(kSampleRate);
    return sys;
}

// A 32 KB ROM whose entire job is to hold PSG tone channel 0 open, then spin.
// The real tracker is the wrong instrument for an amplitude assertion: idle, it
// plays nothing, and if its FM option is on it also mutes the PSG (see the
// FM/PSG mux case below). This isolates "does PSG output reach the host" from
// "is the tracker playing", which is what the ChannelVolumes trap is about.
std::vector<std::uint8_t> psgToneRom() {
    static const std::uint8_t code[] = {
        0x3E, 0x8E,        // ld  a, $8E     latch ch0 tone, low 4 bits of period
        0xD3, 0x7F,        // out ($7F), a
        0x3E, 0x01,        // ld  a, $01     high 6 bits -> ~440 Hz-ish, exact pitch irrelevant
        0xD3, 0x7F,        // out ($7F), a
        0x3E, 0x90,        // ld  a, $90     latch ch0 volume, attenuation 0 = full
        0xD3, 0x7F,        // out ($7F), a
        0x18, 0xFE,        // jr  -2         spin forever
    };
    std::vector<std::uint8_t> rom(32 * 1024, 0x00);
    std::memcpy(rom.data(), code, sizeof(code));
    return rom;
}

// A 32 KB ROM that copies both controller ports into work RAM forever, so a test can see exactly
// what the emulated CPU sees. $C000 <- port $DC (pad bits), $C001 <- port $DD (TR/TL/TH, the sync
// lines). Reading the emulator's own device state instead would prove nothing: the question is
// whether a host-driven level actually reaches an `IN` instruction and is still there a frame later.
std::vector<std::uint8_t> portPollRom() {
    static const std::uint8_t code[] = {
        0xDB, 0xDC,        // in   a, ($DC)
        0x32, 0x00, 0xC0,  // ld   ($C000), a
        0xDB, 0xDD,        // in   a, ($DD)
        0x32, 0x01, 0xC0,  // ld   ($C001), a
        0x18, 0xF4,        // jr   -12          back to the top
    };
    std::vector<std::uint8_t> rom(32 * 1024, 0x00);
    std::memcpy(rom.data(), code, sizeof(code));
    return rom;
}

// A 32 KB ROM that TIMESTAMPS the moment the sync line changes. It counts its own polls while $DD
// reads idle and freezes that count the instant it does not, so the count IS a clock: how long the
// ROM waited, in units of its own poll loop. The port-poll ROM above can only say a level arrived;
// this is what says WHEN, which is the whole claim of the sample-accurate design.
//
// $C000/$C001 = idle-poll count (16-bit, little endian), frozen on change. $C002 = the value seen.
std::vector<std::uint8_t> syncTimestampRom() {
    static const std::uint8_t code[] = {
        0x21, 0x00, 0x00,  // 0000  ld   hl, $0000
        0xDB, 0xDD,        // 0003  in   a, ($DD)      <- loop
        0xFE, 0xFF,        // 0005  cp   $FF
        0x20, 0x06,        // 0007  jr   nz, +6        -> done at 000F
        0x23,              // 0009  inc  hl
        0x22, 0x00, 0xC0,  // 000A  ld   ($C000), hl
        0x18, 0xF4,        // 000D  jr   -12           -> loop at 0003
        0x32, 0x02, 0xC0,  // 000F  ld   ($C002), a    <- done
        0x18, 0xFE,        // 0012  jr   -2            spin, count stays frozen
    };
    std::vector<std::uint8_t> rom(32 * 1024, 0x00);
    std::memcpy(rom.data(), code, sizeof(code));
    return rom;
}

// A 32 KB Game Gear ROM that configures the EXT parallel port's direction and then copies $01 into
// work RAM forever. The GG twin of portPollRom, and the only way to see what an `IN ($01)` actually
// returns: the whole point of the vendored direction-mask edit is that the answer depends on $02, so
// reading emulator state instead of running an IN would test the wrong thing.
//
// `dir` is the $02 word (bits 6-0 = PC6-PC0 direction, 1 = INPUT; bit 7 disables the PC6 NMI), and
// `latch` is written to $01 afterwards, which is what an OUTPUT pin must read back. That write order -
// direction first, then data - is the safe sequence GGSYNC.md specifies for the undefined power-on
// latch. $C001 <- port $01.
std::vector<std::uint8_t> ggExtPollRom(std::uint8_t dir, std::uint8_t latch) {
    const std::uint8_t code[] = {
        0x3E, dir,         // 0000  ld   a, dir
        0xD3, 0x02,        // 0002  out  ($02), a
        0x3E, latch,       // 0004  ld   a, latch
        0xD3, 0x01,        // 0006  out  ($01), a
        0xDB, 0x01,        // 0008  in   a, ($01)     <- loop
        0x32, 0x01, 0xC0,  // 000A  ld   ($C001), a
        0x18, 0xF9,        // 000D  jr   -7            -> loop at 0008
    };
    std::vector<std::uint8_t> rom(32 * 1024, 0x00);
    std::memcpy(rom.data(), code, sizeof(code));
    return rom;
}

// Work RAM as the ROM sees it. SmsWorkRam is 8 KB based at $C000, so index 0 is $C000.
std::uint8_t workRam(MesenSmsSystem& sys, std::size_t index) {
    auto acc = sys.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(acc.valid());
    REQUIRE(acc.size() > index);
    return acc.data()[index];
}

std::unique_ptr<MesenSmsSystem> buildPoller(SystemId id = 1) {
    MesenSmsConfig cfg;
    cfg.gameGear = false;
    cfg.enableFm = false;
    cfg.romPath  = "port-poll.sms";
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, portPollRom());
    sys->onActivate(kSampleRate);
    return sys;
}

std::unique_ptr<MesenSmsSystem> buildGgPoller(std::uint8_t dir, std::uint8_t latch, SystemId id = 1) {
    MesenSmsConfig cfg;
    cfg.gameGear = true;
    cfg.enableFm = false;
    cfg.romPath  = "gg-ext-poll.gg";   // the .gg extension is what selects the GG model in Mesen
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, ggExtPollRom(dir, latch));
    sys->onActivate(kSampleRate);
    return sys;
}

// The sync counter as a Game Gear EXT-port level word: counter bit 0 on PC4 AND PC5, bit 1 on PC6
// (GGSYNC.md). The C++ mirror of ggSyncLevels in smsSync.ts.
std::uint8_t ggSyncLevels(int counter) {
    std::uint8_t levels = 0x7F;
    if (!(counter & 1)) levels &= std::uint8_t(~0x30);   // PC4 + PC5 low
    if (!(counter & 2)) levels &= std::uint8_t(~0x40);   // PC6 low
    return levels;
}

std::uint32_t workRam16(MesenSmsSystem& sys, std::size_t index) {
    auto acc = sys.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(acc.valid());
    REQUIRE(acc.size() > index + 1);
    return std::uint32_t(acc.data()[index]) | (std::uint32_t(acc.data()[index + 1]) << 8);
}

std::unique_ptr<MesenSmsSystem> buildTimestamp(SystemId id = 1) {
    MesenSmsConfig cfg;
    cfg.gameGear = false;
    cfg.enableFm = false;
    cfg.romPath  = "sync-ts.sms";
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, syncTimestampRom());
    sys->onActivate(kSampleRate);
    return sys;
}

// The sync counter as a controller-port level word. Counter bits are active HIGH at the port
// (engine.asm sync_read: `and $0C / cp $0C` for bit 0, `bit 7` for bit 1), so a bit is pulled LOW to
// signal 0. TL (bit 2) is left high, making the ROM's "TR AND TL" reduce to TR.
std::uint8_t syncLevels(int counter) {
    std::uint8_t levels = 0xFF;
    if (!(counter & 1)) levels &= std::uint8_t(~0x08);   // TR low
    if (!(counter & 2)) levels &= std::uint8_t(~0x80);   // TH low
    return levels;
}

std::unique_ptr<MesenSmsSystem> buildSynthetic(bool enableFm, SystemId id = 1) {
    MesenSmsConfig cfg;
    cfg.gameGear = false;
    cfg.enableFm = enableFm;
    cfg.romPath  = "psg-tone.sms";   // stem only; the bytes are staged to disk
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, psgToneRom());
    sys->onActivate(kSampleRate);
    return sys;
}

// One block through the triad. Returns peak absolute sample across both lanes.
float runBlock(MesenSmsSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    l.assign(kFrames, 0.0f);
    r.assign(kFrames, 0.0f);
    float* outs[2] = { l.data(), r.data() };

    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;

    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, outs, 2);

    float peak = 0.0f;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        peak = std::max(peak, std::fabs(l[i]));
        peak = std::max(peak, std::fabs(r[i]));
    }
    return peak;
}

struct Stats { float peak = 0.0f; double meanAbs = 0.0; };

Stats runBlocks(MesenSmsSystem& sys, int blocks) {
    std::vector<float> l, r;
    Stats st;
    double sum = 0.0;
    std::uint64_t n = 0;
    for (int b = 0; b < blocks; ++b) {
        st.peak = std::max(st.peak, runBlock(sys, l, r));
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            sum += std::fabs(l[i]) + std::fabs(r[i]);
            n += 2;
        }
    }
    st.meanAbs = n ? sum / static_cast<double>(n) : 0.0;
    return st;
}

// Drive `blocks` blocks of `frames` each, appending every drained sample (interleaved L,R) to `out`.
// Returns the max ring residue observed after any block: availableFrames() beyond the target the
// step loop was asked for.
std::uint32_t runInto(MesenSmsSystem& sys, std::uint32_t frames, int blocks, std::vector<float>& out) {
    std::vector<float> l(frames), r(frames);
    float* outs[2] = { l.data(), r.data() };
    std::uint32_t maxResidue = 0;

    AudioBlockInfo info{};
    info.frames     = frames;
    info.sampleRate = kSampleRate;

    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(frames)) {}
        const std::uint32_t have = sys.availableFrames();
        maxResidue = std::max(maxResidue, have > frames ? have - frames : 0u);
        sys.finishBlock(info, outs, 2);
        for (std::uint32_t i = 0; i < frames; ++i) { out.push_back(l[i]); out.push_back(r[i]); }
    }
    return maxResidue;
}

} // namespace

TEST_CASE("SMS host-driven port levels reach the ROM and hold", "[sms]") {
    // Guards the vendored SmsControlManager::SetExternalInput. The sync counter smsggdj reads off
    // $DD needs TH (bit 7) AND TR (bit 3) driveable; stock Mesen can drive neither from the host,
    // because SmsController::ReadRam(addr == 1) only ever clears bits 0x01/0x02/0x04/0x08. Without
    // the edit $DD is pinned at 0xFF and the 2-bit counter collapses to mod-2, which reads as an
    // intermittent double-tempo bug rather than dead sync.
    auto sys = buildPoller();
    REQUIRE(sys->activated());

    std::vector<float> scratch;
    runInto(*sys, kFrames, 4, scratch);
    CHECK(workRam(*sys, 1) == 0xFF);   // idle: every line released

    // Each of the three sync lines, driven individually, must appear at the ROM's IN.
    struct Line { const char* name; std::uint8_t levels; };
    for (const Line& ln : { Line{"TR", std::uint8_t(0xF7)},      // bit 3
                            Line{"TH", std::uint8_t(0x7F)},      // bit 7
                            Line{"TR+TH", std::uint8_t(0x77)} }) {
        INFO("line = " << ln.name);
        sys->setExternalInput(1, ln.levels);
        runInto(*sys, kFrames, 2, scratch);
        CHECK(workRam(*sys, 1) == ln.levels);

        // And it must still be there several frames later. A level the ROM samples once per video
        // frame is useless if the host has to re-assert it: set-and-hold is the contract.
        runInto(*sys, kFrames, 8, scratch);
        CHECK(workRam(*sys, 1) == ln.levels);
    }

    sys->setExternalInput(1, 0xFF);
    runInto(*sys, kFrames, 2, scratch);
    CHECK(workRam(*sys, 1) == 0xFF);   // released again
}

TEST_CASE("GG host-driven EXT levels reach the ROM, and only on input pins", "[sms]") {
    // Guards the vendored SmsMemoryManager $01 read. Stock Mesen returned _state.GgExtData verbatim -
    // a bare loopback, flagged //TODOSMS - so an `IN ($01)` could only ever see what the ROM itself
    // had written. smsggdj's GG build decodes its sync counter from PC4/PC5/PC6 there, which meant
    // the counter was pinned at whatever the startup latch left and `(current - last) & 3` was
    // permanently 0: armed, willing, and silent forever.
    std::vector<float> scratch;

    SECTION("all pins input: the host drives every bit") {
        auto sys = buildGgPoller(/*dir=*/0xFF, /*latch=*/0xFF);   // $FF = PC0-PC6 all inputs
        REQUIRE(sys->activated());
        runInto(*sys, kFrames, 4, scratch);
        // Idle reads exactly what the stock loopback returned after the same init, so the edit is
        // inert until something drives a pin. This is the no-regression half of the guard.
        CHECK(workRam(*sys, 1) == 0x7F);

        // Each counter level in turn, and each must still be there several frames later: the ROM
        // samples once per video frame, so set-and-hold is the contract exactly as it is on SMS.
        for (int counter = 0; counter < 4; ++counter) {
            INFO("counter = " << counter);
            const std::uint8_t levels = ggSyncLevels(counter);
            sys->setExternalInput(0, levels);
            runInto(*sys, kFrames, 2, scratch);
            CHECK(workRam(*sys, 1) == levels);
            runInto(*sys, kFrames, 8, scratch);
            CHECK(workRam(*sys, 1) == levels);
        }

        sys->setExternalInput(0, 0x7F);
        runInto(*sys, kFrames, 2, scratch);
        CHECK(workRam(*sys, 1) == 0x7F);   // released again
    }

    SECTION("output pins ignore the host and read back the latch") {
        // $8F is GGSYNC.md's "sync OUT" mask: PC4-PC6 outputs, PC0-PC3 inputs, PC6 NMI disabled. So
        // with the host pulling everything low, the four input bits follow it to 0 and the three
        // output bits must still read the latched 1s. Without the direction mask this would read
        // 0x00 (host wins everywhere) or 0x7F (latch wins everywhere); only honouring $02 gives 0x70.
        auto sys = buildGgPoller(/*dir=*/0x8F, /*latch=*/0xFF);
        REQUIRE(sys->activated());
        runInto(*sys, kFrames, 4, scratch);
        CHECK(workRam(*sys, 1) == 0x7F);

        sys->setExternalInput(0, 0x00);
        runInto(*sys, kFrames, 4, scratch);
        CHECK(workRam(*sys, 1) == 0x70);
    }

    SECTION("bit 7 of $02 is the NMI disable, not a direction") {
        // $7F and $FF differ only in bit 7, which Sega's manual defines as the PC6 falling-edge NMI
        // disable. Reading it as an eighth direction bit would make these two masks behave
        // differently; they must not. (gg_link_test.asm really does use $7F.)
        auto a = buildGgPoller(/*dir=*/0x7F, /*latch=*/0xFF);
        auto b = buildGgPoller(/*dir=*/0xFF, /*latch=*/0xFF, /*id=*/2);
        REQUIRE(a->activated());
        REQUIRE(b->activated());
        a->setExternalInput(0, 0x2A);
        b->setExternalInput(0, 0x2A);
        runInto(*a, kFrames, 4, scratch);
        runInto(*b, kFrames, 4, scratch);
        CHECK(workRam(*a, 1) == 0x2A);
        CHECK(workRam(*a, 1) == workRam(*b, 1));
    }
}

TEST_CASE("SMS held button survives an emulated frame", "[sms]") {
    // Guards the vendored SmsControlManager::UpdateInputState override. SmsConsole::ProcessEndOfFrame
    // calls UpdateInputState once per video frame, and the base implementation runs ClearState() +
    // SetStateFromInput() per device - wiping any bit the host set via SetBitValue. Without the
    // override a held button survives less than one frame, so it is not a "buttons feel laggy" bug,
    // it is "buttons do nothing".
    auto sys = buildPoller();
    REQUIRE(sys->activated());

    std::vector<float> scratch;
    runInto(*sys, kFrames, 4, scratch);
    CHECK(workRam(*sys, 0) == 0xFF);   // nothing pressed

    // Port 0, button A -> $DC bit 5 clears (active low).
    sys->pressButton(static_cast<std::uint8_t>(SmsButton::A), true);
    runInto(*sys, kFrames, 2, scratch);   // several emulated frames, so the clobber would have hit
    CHECK((workRam(*sys, 0) & 0x20) == 0);

    // Still held much later.
    runInto(*sys, kFrames, 10, scratch);
    CHECK((workRam(*sys, 0) & 0x20) == 0);

    sys->pressButton(static_cast<std::uint8_t>(SmsButton::A), false);
    runInto(*sys, kFrames, 2, scratch);
    CHECK(workRam(*sys, 0) == 0xFF);
}

TEST_CASE("SMS step loop never leaves a block's worth of audio in the ring", "[sms]") {
    // The step loop stops when the ring reaches the block target, so it always overshoots a little.
    // What must NOT happen is the residue reaching a whole block: the next block would then be
    // satisfied entirely from the ring, `stepIfBelowTarget` would never enter its loop, and a block
    // that never enters the loop never pumps scheduled events - a sync event silently dropped rather
    // than delivered late.
    //
    // Measured before the coarse-window clamp in stepIfBelowTarget: 131 at a 128-frame block and 172
    // at 199, i.e. MORE than a whole block, on ordinary DAW buffer sizes. After: 28 and 11.
    for (std::uint32_t bs : { 32u, 64u, 128u, 199u, 256u, 512u, 1024u, 2048u }) {
        INFO("blockSize = " << bs);
        auto sys = buildSynthetic(/*enableFm=*/false, 200 + bs);
        REQUIRE(sys->activated());
        std::vector<float> audio;
        const std::uint32_t residue = runInto(*sys, bs, int(20480 / bs), audio);
        INFO("residue = " << residue);
        CHECK(residue < bs);    // the no-stall invariant: every block still steps the CPU
        CHECK(residue <= 64);   // and stays small in absolute terms (measured max 28)
    }
}

TEST_CASE("SMS audio is invariant to the host block size", "[sms]") {
    // The step loop flushes the PSG on a cycle budget, not on ROM behaviour, and the block size
    // changes that cadence completely: at 64 frames the fine tail engages immediately, at 1024 it is
    // coarse until the last 64. If flushing at arbitrary points lost or duplicated a sample, the two
    // streams would diverge. They do not, because blip_end_frame carries the sub-sample remainder in
    // blip_t::offset and SmsPsg::Run carries the sub-16-cycle remainder in _masterClock.
    //
    // This is the property the whole sample-accurate design rests on, and it is why the cadence is
    // free to be tuned for FM without touching timing. FM off: with FM on, SmsFmAudio resamples with
    // fillToMax and the cadence DOES change the output (see MesenSmsConfig::enableFm).
    std::vector<float> reference;
    {
        auto sys = buildSynthetic(/*enableFm=*/false, 300);
        REQUIRE(sys->activated());
        runInto(*sys, 1024, 20, reference);
    }
    REQUIRE(reference.size() == 1024 * 20 * 2);

    for (std::uint32_t bs : { 32u, 64u, 128u, 199u, 256u, 512u, 2048u }) {
        INFO("blockSize = " << bs);
        auto sys = buildSynthetic(/*enableFm=*/false, 400 + bs);
        REQUIRE(sys->activated());
        std::vector<float> got;
        runInto(*sys, bs, int(20480 / bs), got);

        const std::size_t n = std::min(got.size(), reference.size());
        REQUIRE(n > 0);
        std::size_t firstDiff = SIZE_MAX;
        for (std::size_t i = 0; i < n; ++i) {
            if (got[i] != reference[i]) { firstDiff = i; break; }
        }
        INFO("firstDiff = " << (firstDiff == SIZE_MAX ? std::string("NONE") : std::to_string(firstDiff)));
        CHECK(firstDiff == SIZE_MAX);
    }
}

TEST_CASE("SMS gate metric tracks the audio ring", "[sms]") {
    // intraBlockSamplePos() converts elapsed Z80 cycles to an intra-block sample position, and it is
    // what scheduled events will be released against - NOT availableFrames(), which only moves when
    // the PSG flushes. That decoupling is what makes SMS event delivery finer than the NES's, but it
    // only works if the conversion is right: a wrong master clock (PAL vs NTSC, say) would drift the
    // two apart and every sync event would land progressively early or late.
    //
    // Sampled at flush boundaries, where the two should agree almost exactly.
    auto sys = buildSynthetic(/*enableFm=*/false, 500);
    REQUIRE(sys->activated());

    std::vector<float> warm;
    runInto(*sys, 512, 4, warm);   // past boot, into steady state

    AudioBlockInfo info{};
    info.frames     = 2048;
    info.sampleRate = kSampleRate;
    sys->prepareForBlock(info);

    for (std::uint32_t target : { 128u, 256u, 512u, 1024u, 1536u, 2048u }) {
        while (sys->stepIfBelowTarget(target)) {}
        const int pos  = int(sys->intraBlockSamplePos());
        const int ring = int(sys->availableFrames());
        INFO("target = " << target << "  pos = " << pos << "  ring = " << ring);
        CHECK(std::abs(pos - ring) <= 4);   // measured <= 1
    }
}

// Calibrate the timestamp ROM's poll rate: with the line never changing it polls freely for a whole
// block, so iterations/frames is its rate. Measured empirically rather than derived from a T-state
// table, so it stays correct across instruction-timing details and region clock changes.
double calibratePollRate(std::uint32_t blockSize) {
    auto sys = buildTimestamp(600);
    REQUIRE(sys->activated());
    std::vector<float> scratch;
    runInto(*sys, blockSize, 4, scratch);            // past boot
    const std::uint32_t a = workRam16(*sys, 0);
    runInto(*sys, blockSize, 1, scratch);
    const std::uint32_t b = workRam16(*sys, 0);
    REQUIRE(b > a);
    return double(b - a) / double(blockSize);
}

TEST_CASE("SMS sync levels are delivered at their scheduled sample offset", "[sms]") {
    // THE assertion this backend exists for, and the first one that tests the whole sentence rather
    // than half of it. Commit 2 proved a host-driven level reaches the emulated IN, and that the step
    // loop advances its sample position at the right rate. This proves a level scheduled for sample N
    // is observed by the CPU at sample N.
    //
    // Delivery is gated on intraBlockSamplePos() (the Z80 cycle counter), never on availableFrames(),
    // so the accuracy here is independent of the PSG flush cadence. Deliver everything at block start
    // instead - which is what MesenGbaSystem does, having no offset path at all - and the error
    // becomes -k rather than a fraction of a sample.
    constexpr std::uint32_t kBlock = 2048;
    const double pollsPerSample = calibratePollRate(kBlock);
    INFO("pollsPerSample = " << pollsPerSample);

    for (std::uint32_t k : { 0u, 1u, 64u, 256u, 1024u, 2000u }) {
        INFO("scheduled offset = " << k);
        auto sys = buildTimestamp(610 + k);
        REQUIRE(sys->activated());
        std::vector<float> scratch;
        runInto(*sys, kBlock, 4, scratch);
        const std::uint32_t c0 = workRam16(*sys, 0);

        const std::uint8_t lv = syncLevels(0);      // counter 0 -> TR and TH low, so != idle 0xFF
        sys->pushCoreBytes(k, &lv, 1, false);
        runInto(*sys, kBlock, 1, scratch);

        CHECK(workRam(*sys, 2) == lv);              // the ROM saw the level we sent
        const double observed = double(workRam16(*sys, 0) - c0) / pollsPerSample;
        INFO("observed offset = " << observed);
        // Measured error is at most ~1 sample and consistently slightly early: the ROM samples the
        // port once per ~0.8 samples, so it can catch a level up to one poll before this measurement
        // origin. 3 is that with headroom, and still three orders of magnitude tighter than the
        // block-start delivery it has to distinguish from.
        CHECK(std::fabs(observed - double(k)) <= 3.0);
    }
}

TEST_CASE("SMS sync level past the block end carries into the next block", "[sms]") {
    // The DAW hands events offsets within the current block, but a role computing tick positions can
    // legitimately schedule past its end. Such a level must fire early in the NEXT block at
    // offset - frames: dropping it loses a clock (the ROM reads levels, so a skipped counter value is
    // a skipped beat), and firing it immediately puts the clock early.
    constexpr std::uint32_t kBlock = 2048;
    constexpr std::uint32_t kOver  = 500;    // into the following block
    const double pollsPerSample = calibratePollRate(kBlock);

    auto sys = buildTimestamp(700);
    REQUIRE(sys->activated());
    std::vector<float> scratch;
    runInto(*sys, kBlock, 4, scratch);

    const std::uint8_t lv = syncLevels(0);
    sys->pushCoreBytes(kBlock + kOver, &lv, 1, false);

    // Block one: must NOT fire. The ROM has written nothing to its "seen" slot.
    const std::uint32_t c0 = workRam16(*sys, 0);
    runInto(*sys, kBlock, 1, scratch);
    CHECK(workRam(*sys, 2) == 0x00);
    const std::uint32_t c1 = workRam16(*sys, 0);
    CHECK(c1 > c0);                                  // still polling, not frozen

    // Block two: fires at kOver into it.
    runInto(*sys, kBlock, 1, scratch);
    CHECK(workRam(*sys, 2) == lv);
    const double observed = double(workRam16(*sys, 0) - c1) / pollsPerSample;
    INFO("observed offset into second block = " << observed);
    CHECK(std::fabs(observed - double(kOver)) <= 3.0);
}

TEST_CASE("SMS sync levels are applied in offset order, not enqueue order", "[sms]") {
    // Two feeds can enqueue into one block out of order, so the queue stable-sorts once before
    // draining. Getting this wrong on a LEVEL transport is worse than on a byte stream: the ROM reads
    // (current - last) & 3 per frame, so levels arriving out of order make the counter run backwards
    // and the tracker stutters rather than simply mistimes.
    constexpr std::uint32_t kBlock = 2048;
    const double pollsPerSample = calibratePollRate(kBlock);

    // Timestamp ROM freezes on the FIRST change, so it reports which level was applied EARLIEST.
    {
        auto sys = buildTimestamp(800);
        REQUIRE(sys->activated());
        std::vector<float> scratch;
        runInto(*sys, kBlock, 4, scratch);
        const std::uint32_t c0 = workRam16(*sys, 0);

        const std::uint8_t l3 = syncLevels(3), l1 = syncLevels(1), l2 = syncLevels(2);
        sys->pushCoreBytes(1500, &l3, 1, false);     // enqueued first, due last
        sys->pushCoreBytes(100,  &l1, 1, false);     // enqueued second, due FIRST
        sys->pushCoreBytes(800,  &l2, 1, false);
        runInto(*sys, kBlock, 1, scratch);

        CHECK(workRam(*sys, 2) == l1);               // the offset-100 level, not the first enqueued
        const double observed = double(workRam16(*sys, 0) - c0) / pollsPerSample;
        INFO("observed offset = " << observed);
        CHECK(std::fabs(observed - 100.0) <= 3.0);
    }

    // Port-poll ROM stores continuously, so it reports which level is left standing at block end.
    {
        auto sys = buildPoller(801);
        REQUIRE(sys->activated());
        std::vector<float> scratch;
        runInto(*sys, kBlock, 4, scratch);

        const std::uint8_t l3 = syncLevels(3), l1 = syncLevels(1), l2 = syncLevels(2);
        sys->pushCoreBytes(1500, &l3, 1, false);
        sys->pushCoreBytes(100,  &l1, 1, false);
        sys->pushCoreBytes(800,  &l2, 1, false);
        runInto(*sys, kBlock, 1, scratch);

        // Last BY OFFSET wins. Unsorted, the offset-800 level would have been applied last.
        CHECK(workRam(*sys, 1) == l3);
    }
}

TEST_CASE("SMS and GG ROMs boot into a live core", "[sms]") {
    for (bool gg : { false, true }) {
        INFO("gameGear = " << gg);
        auto sys = build(gg);
        REQUIRE(sys->activated());
        REQUIRE(sys->kind() == SystemKind::MesenSms);

        // Advancing must actually produce audio frames, which means the step loop drove the CPU and
        // flushed the PSG. A loop that never flushes returns the same zero count forever.
        std::vector<float> l, r;
        runBlock(*sys, l, r);
        REQUIRE(sys->framebuffer() != nullptr);
    }
}

TEST_CASE("SMS and GG render at their visible resolutions", "[sms]") {
    SECTION("Master System is 256x192") {
        auto sys = build(false);
        REQUIRE(sys->activated());
        runBlocks(*sys, 8);   // a few frames so the VDP has emitted at least one
        CHECK(sys->framebuffer()->width()  == MesenSmsSystem::kSmsPixelWidth);
        CHECK(sys->framebuffer()->height() == MesenSmsSystem::kSmsPixelHeight);
        // The core's own frame must match, or the overscan config and the constants have drifted
        // and MesenVideoDevice is silently cropping. Unset overscan gives 256x240 here.
        CHECK(sys->coreFrameWidth()  == MesenSmsSystem::kSmsPixelWidth);
        CHECK(sys->coreFrameHeight() == MesenSmsSystem::kSmsPixelHeight);
    }

    SECTION("Game Gear is 160x144") {
        auto sys = build(true);
        REQUIRE(sys->activated());
        runBlocks(*sys, 8);
        CHECK(sys->framebuffer()->width()  == MesenSmsSystem::kGgPixelWidth);
        CHECK(sys->framebuffer()->height() == MesenSmsSystem::kGgPixelHeight);
        // Without GameGearOverscan = {48,48,48,48} this is 256x240 and the tile shows a small
        // image floating in a mostly-black frame.
        CHECK(sys->coreFrameWidth()  == MesenSmsSystem::kGgPixelWidth);
        CHECK(sys->coreFrameHeight() == MesenSmsSystem::kGgPixelHeight);
    }
}

TEST_CASE("SMS PSG is not silent", "[sms]") {
    // THE assertion this whole file exists for. With ChannelVolumes at its {0,0,0,0} default, every
    // channel is multiplied by 0/100: the sample COUNT stays correct and every sample is zero, so
    // nothing upstream notices and a boot test passes.
    auto sys = buildSynthetic(/*enableFm=*/false);
    REQUIRE(sys->activated());

    const Stats st = runBlocks(*sys, 16);
    INFO("peak = " << st.peak << "  meanAbs = " << st.meanAbs);
    CHECK(st.peak > 0.01f);
    CHECK(st.meanAbs > 0.0);
}

TEST_CASE("SMS FM enable mutes the PSG (Mesen models $F2 as a mux)", "[sms]") {
    // Pins down behaviour that is easy to mistake for a bug in this backend, and that decides
    // whether enableFm can default true for a tracker.
    //
    // Mesen implements the Japanese SMS rule: writing 1 or 2 to $F2 selects FM and SILENCES the
    // PSG (SmsFmAudio::IsPsgAudioMuted -> SmsPsg::PlayQueuedAudio memsets the buffer). smsggdj
    // writes $F2=$01 at boot when its FM option is on, and its own source says real hardware and
    // SMSPlus SUM the two while Emulicious muxes. So on Mesen an FM-enabled smsggdj loses every
    // PSG channel it plays.
    //
    // The synthetic ROM never touches $F2, so it is audible either way; this asserts the routing
    // gate instead - with enableFm false, $F0/$F1/$F2 never reach the FM unit at all
    // (SmsMemoryManager.cpp:373), which is what makes the mute escapable.
    auto fmOff = buildSynthetic(/*enableFm=*/false, 1);
    auto fmOn  = buildSynthetic(/*enableFm=*/true,  2);
    REQUIRE(fmOff->activated());
    REQUIRE(fmOn->activated());

    const Stats off = runBlocks(*fmOff, 16);
    const Stats on  = runBlocks(*fmOn,  16);
    INFO("fm off peak = " << off.peak << "  fm on peak = " << on.peak);

    // A ROM that leaves $F2 alone keeps its PSG under both settings.
    CHECK(off.peak > 0.01f);
    CHECK(on.peak  > 0.01f);
}

TEST_CASE("SMS construct/destruct cycles do not crash", "[sms]") {
    // The teardown UAF is nondeterministic (measured ~3 in 5 runs of 40 cycles without the
    // Emulator::Stop in onDeactivate), so the repetition IS the test. Each system boots and runs a
    // block so the FM unit is genuinely live before it is torn down.
    for (int i = 0; i < 40; ++i) {
        auto sys = build(i % 2 == 0, static_cast<SystemId>(i + 1));
        REQUIRE(sys->activated());
        std::vector<float> l, r;
        runBlock(*sys, l, r);
        sys.reset();
    }
    SUCCEED("40 construct/destruct cycles survived");
}
