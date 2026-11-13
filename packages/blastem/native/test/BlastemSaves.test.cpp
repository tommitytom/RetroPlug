// Savestates (design section 2): two instances seeded with one state render identical bytes, a restored render is not
// silent, and a state whose serializer identifier is not BlastEm's 1.0 one (or whose body is truncated) is refused
// rather than misread.
//
// The last two cases are the whole point: BlastEm's own serializer calls fatal_error, which exit()s the process, on a
// short read. A plugin handed a truncated or foreign state has to return an error and keep its core, so reaching a
// CHECK at all is the proof, and a live render after it is the proof the refusal left a machine behind.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"  // IO_CUSTOM, io_port
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

// The largest |sample| in a render: a restored render has to be audible, or "the state reached the core" is unproven.
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom,
                                                   std::vector<std::uint8_t> state = {}) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    cfg.savestate = std::move(state);
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

std::vector<float> run(rp::blastem::BlastemMdSystem& sys, int blocks) {
    std::vector<float> l(kFrames), r(kFrames), out;
    float*             lanes[2] = {l.data(), r.data()};
    AudioBlockInfo     info{};
    info.frames = kFrames;
    for (int i = 0; i < blocks; ++i) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(kFrames)) {}
        sys.finishBlock(info, lanes, 2);
        out.insert(out.end(), l.begin(), l.end());
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

// The byte a ROM that stores its port-2 read at $FF0000 left there, straight out of BlastEm's word-ordered work RAM.
std::uint8_t ramByte(rp::blastem::BlastemMdSystem& sys) {
    return static_cast<std::uint8_t*>(static_cast<void*>(sys.core()->work_ram))[1];
}

// serialize.h's section ids, for the two cases that reach one field of a real state without assuming the layout of the
// rest of it.
constexpr std::size_t kSectionMainRam = 10;   // SECTION_MAIN_RAM
constexpr std::size_t kSectionZ80     = 2;    // SECTION_Z80
constexpr std::size_t kSectionVdp     = 3;    // SECTION_VDP
constexpr std::size_t kSectionYm      = 4;    // SECTION_YM2612
constexpr std::size_t kSectionPsg     = 5;    // SECTION_PSG
constexpr std::size_t kSection68000   = 1;    // SECTION_68000
constexpr std::size_t kNoSection      = ~std::size_t(0);

// The offset of section `id`'s first payload byte in a whole state (identifier included), or kNoSection. The same
// section layout sectionsFit walks in the system: a u16 id, a u32 size, then exactly that many bytes.
std::size_t sectionPayload(const std::vector<std::uint8_t>& state, std::size_t id) {
    constexpr std::size_t kIdent = 8;  // the identifier saveStateBytes prefixes
    std::size_t           at     = kIdent;
    while (at + 6 <= state.size()) {
        const std::size_t   sid = (std::size_t(state[at]) << 8) | std::size_t(state[at + 1]);
        const std::uint32_t len = (std::uint32_t(state[at + 2]) << 24) | (std::uint32_t(state[at + 3]) << 16) |
                                  (std::uint32_t(state[at + 4]) << 8) | std::uint32_t(state[at + 5]);
        if (sid == id) return at + 6;
        if (len > state.size() - at - 6) return kNoSection;
        at += 6 + len;
    }
    return kNoSection;
}

// One section as the walk sees it: its id and its payload, so a case can rebuild a state with a section missing, one
// repeated, or one relabelled, without knowing where in the stream anything is.
struct RawSection {
    std::uint16_t             id;
    std::vector<std::uint8_t> payload;
};

std::vector<RawSection> splitSections(const std::vector<std::uint8_t>& state) {
    std::vector<RawSection> out;
    std::size_t             at = 8;  // the identifier saveStateBytes prefixes
    while (at + 6 <= state.size()) {
        const std::uint16_t id  = std::uint16_t(state[at] << 8 | state[at + 1]);
        const std::uint32_t len = (std::uint32_t(state[at + 2]) << 24) | (std::uint32_t(state[at + 3]) << 16) |
                                  (std::uint32_t(state[at + 4]) << 8) | std::uint32_t(state[at + 5]);
        if (len > state.size() - at - 6) break;
        out.push_back({id, std::vector<std::uint8_t>(state.begin() + at + 6, state.begin() + at + 6 + len)});
        at += 6 + len;
    }
    return out;
}

std::vector<std::uint8_t> assembleSections(const std::vector<RawSection>& sections) {
    std::vector<std::uint8_t> out{'B', 'L', 'S', 'T', 'S', 'Z', 0x01, 0x07};
    for (const RawSection& s : sections) {
        const std::uint32_t len = static_cast<std::uint32_t>(s.payload.size());
        out.push_back(std::uint8_t(s.id >> 8));
        out.push_back(std::uint8_t(s.id));
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(std::uint8_t(len >> shift));
        out.insert(out.end(), s.payload.begin(), s.payload.end());
    }
    return out;
}

}  // namespace

TEST_CASE("two instances seeded with one state render identical bytes", "[blastem][saves]") {
    // The round trip as a sink can honestly prove it: the sink's ring, its blips and its filter are the HOST's state,
    // not the console's, so a restored run is not byte-identical to the original run. Two FRESH instances seeded with
    // the same bytes each run through the same sink, so their renders are the comparison that means something, and a
    // restored render that is not silent is the check that the state reached the core at all.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 20);  // boot, and let the note sound
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    REQUIRE(state.size() > 8);
    CHECK(std::memcmp(state.data(), "BLSTSZ\x01\x07", 8) == 0);  // BlastEm's own serializer identifier
    auto first  = boot(rp::blastem::test::fmPsgRom(), state);
    auto second = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(first->activated());
    CHECK(second->activated());
    CHECK_FALSE(first->stateRefused());
    const std::vector<float> a = run(*first, 40);
    const std::vector<float> b = run(*second, 40);
    CHECK(a == b);
    CHECK(peak(a) > 0.01);  // the restored core is playing, not frozen at the state's first sample
}

TEST_CASE("a loaded state keeps port 2 as the host's device", "[blastem][saves]") {
    // The state carries the port's device type (io_serialize writes IO_CUSTOM), and it carries no host state: the
    // queue and the held level belong to the instance. So a load has to leave port 2 the host's device with its pins
    // released, or a state saved while a transport was holding a pin comes back as that pin held.
    auto sys = boot(rp::blastem::test::portPollLoopRom());
    const std::uint8_t low = 0x00;
    sys->pushCoreBytes(10, &low, 1);
    run(*sys, 2);
    CHECK((ramByte(*sys) & 0x60) == 0x00);  // the held level reached the ROM
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    REQUIRE(state.size() > 8);
    auto restored = boot(rp::blastem::test::portPollLoopRom(), state);
    REQUIRE(restored->activated());
    CHECK_FALSE(restored->stateRefused());
    CHECK(restored->core()->io.ports[1].device_type == IO_CUSTOM);
    CHECK(restored->portDevice().lastApplied() == 0x60);
    run(*restored, 2);
    CHECK(ramByte(*restored) == 0x7F);  // the pull-ups, not the state's low level
}

TEST_CASE("a state from another serializer version is refused", "[blastem][saves]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state[7] = 0x08;  // "BLSTSZ\x01\x08": another version
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    // A refused state leaves the core at power-on: the ROM's first block still renders, and the state's bytes never
    // reached the emulator.
    CHECK(restored->activated());
    CHECK(restored->stateRefused());
    CHECK(peak(run(*restored, 4)) > 0.01);
}

TEST_CASE("a truncated state is refused, and the core still runs", "[blastem][saves]") {
    // The case is the RETURN, not the message: BlastEm's own serializer exits the process on a short read
    // (fatal_error, serialize.c:216 and its load_* helpers), so reaching the next CHECK at all is the proof, and the
    // render after it is the proof that the refusal left a live core behind.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state.resize(state.size() / 2);
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    REQUIRE(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);  // from power-on, not a half-loaded state
}

TEST_CASE("a state whose section sizes are lies is refused before the core is touched", "[blastem][saves]") {
    // A body that keeps its total length but lengthens its first section's header field: the walk of the section
    // headers sees the lie, and the run after it starts from power-on rather than from a partial load.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state[8 + 2] = 0xFF;  // the first section's u32 size, at the top of the body
    state[8 + 3] = 0xFF;
    state[8 + 4] = 0xFF;
    state[8 + 5] = 0xFF;
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);
}

TEST_CASE("a state whose section carries an impossible field is refused after a partial load", "[blastem][saves]") {
    // The refusal the walk cannot see: every section header adds up, and the lie is a field inside one of them. BlastEm's
    // own ram_deserialize used to exit() here; the mirror latches, the section propagates the latch, and this case is
    // boot()'s retry: the core has taken some sections by then, so surviving it means the machine was rebuilt rather
    // than restored halfway.
    auto                      sys   = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         ram   = sectionPayload(state, kSectionMainRam);
    REQUIRE(ram != kNoSection);  // a state without a work RAM section would make this case prove nothing
    state[ram] = 0xFF;           // the section's declared size in KiB: 255 KiB of work RAM in a 64 KiB machine
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);  // the power-on machine, not the one the partial load left
}

TEST_CASE("a foreign state is refused with the bytes of another emulator's format", "[blastem][saves]") {
    // A state of some other emulator's shape: right length, no BlastEm identifier anywhere near its front.
    std::vector<std::uint8_t> foreign(4096, 0xA5);
    auto                      restored = boot(rp::blastem::test::fmPsgRom(), foreign);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);
}

TEST_CASE("a cart the database maps past the end of its own image is refused, not fatal", "[blastem][saves]") {
    // romdb.c checked every map entry's offset against the image and called fatal_error when it did not fit, so a
    // trimmed or overdumped copy of a game the database knows took the host process with it. NBA Jam TE's entry maps a
    // second ROM bank at offset $200000; this is a 64 KiB image carrying that product code, so the offset is sixteen
    // times the image. The refusal is the build's: no activated core, and the process still here to answer the next one.
    std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    std::memcpy(rom.data() + 0x180, "T-81406", 7);  // romdb.h's GAME_ID_OFF
    auto refused = boot(std::move(rom));
    CHECK_FALSE(refused->activated());
    auto ok = boot(rp::blastem::test::fmPsgRom());
    CHECK(ok->activated());  // the refusal returned instead of exiting
}

TEST_CASE("a state whose VDP VRAM size runs past the section is refused", "[blastem][saves]") {
    // The VDP section's second byte is the VRAM size in KiB (vdp_serialize's version byte, then vramk). A size above
    // 64 KiB makes vdp_deserialize skip the excess before it reads the rest of the section, and that skip used to be
    // unbounded: the section buffer's position moved past its own size, every later length check wrapped to a huge
    // size_t and passed, and the section's bytes beyond it were read out of the caller's buffer. 130 KiB keeps the
    // misread inside that buffer, so this case fails on the refusal and not on a crash (a 255 KiB size reaches about
    // 120 KB past it, which the review's sanitizer build caught as a heap read and a release build silently accepted).
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         vdp   = sectionPayload(state, kSectionVdp);
    REQUIRE(vdp != kNoSection);
    state[vdp + 1] = 130;  // the VRAM size: 130 KiB, more than twice what this machine has
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);  // from power-on, not from the misread section
}

TEST_CASE("a state's PSG volume is clamped to the channel's four bits", "[blastem][saves]") {
    // psg_deserialize loads the four volume bytes raw, and psg_run indexes volume_table with them: a state's byte above
    // 0xF is a read past that table. The mirror masks each one, as psg_write does, so the state loads (the volume is a
    // value, not an address) and the channel plays its masked level.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         psg   = sectionPayload(state, kSectionPsg);
    REQUIRE(psg != kNoSection);
    // psg_serialize writes the noise LFSR, four counter loads, four counters, then the four volumes.
    const std::size_t volume = psg + 2 + 4 * 2 + 4 * 2;
    state[volume] = 0xF0;
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK_FALSE(restored->stateRefused());
    REQUIRE(restored->activated());
    CHECK((restored->core()->psg->volume[0] & 0xF0) == 0);  // masked, so the table index is in range
    CHECK(peak(run(*restored, 4)) > 0.01);
}

TEST_CASE("a state's YM block and fnum are clamped to the register's bits", "[blastem][saves]") {
    // ym_deserialize loads block and fnum raw and turns them into a keycode, which is an index into detune_table at run
    // time; ym_data_write produces block & 7 and fnum & 0x7FF, so the mirror masks a loaded state the same way. The
    // image is fmOnlyRom because it is the one that really plays the YM.
    auto sys = boot(rp::blastem::test::fmOnlyRom());
    run(*sys, 20);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         ym    = sectionPayload(state, kSectionYm);
    REQUIRE(ym != kNoSection);
    // ym_serialize writes the part 1 registers, the part 2 registers, the 24 operators, then each channel's output,
    // op1_old, block (u8), fnum (u16) and keyon.
    const std::size_t channels = ym + (0xB8 - 0x21) + (0xB8 - 0x30) + 24 * (4 + 2 + 2 + 1 + 1);
    state[channels + 4] = 0xFF;       // channel 0's block
    state[channels + 5] = 0xFF;       // its fnum's high byte
    state[channels + 6] = 0xFF;       // its fnum's low byte
    auto restored = boot(rp::blastem::test::fmOnlyRom(), state);
    CHECK_FALSE(restored->stateRefused());
    REQUIRE(restored->activated());
    CHECK(restored->core()->ym->channels[0].block <= 7);
    CHECK(restored->core()->ym->channels[0].fnum <= 0x7FF);
    CHECK(peak(run(*restored, 20)) > 0.01);  // the detune table is indexed inside its bounds
}

namespace {

// vdp_serialize's payload up to the FIFO's size byte and up to the four sprite counters, in vdp.h's constants: the
// version and VRAM-size bytes, 64 KiB of VRAM, CRAM, VSRAM, the SAT cache, the 24 registers, the two addresses and cd,
// then the FIFO size byte and its entries (12 bytes each), flags, frame, vcounter, hslot, hv_latch, state, the scroll
// and colour latches, the two 32-byte scroll buffers and their two offsets.
constexpr std::size_t kVdpFifoSizeAt = 2 + 64 * 1024 + 128 + 128 + 320 + 24 + 2 * 4 + 1;
constexpr std::size_t kVdpAfterFifo  = 2 + 4 + 2 + 1 + 2 + 1 + 2 + 2 + 4 + 2 + 2 + 2 + 32 + 32 + 1 + 1;

std::size_t vdpAfterFifo(const std::vector<std::uint8_t>& state, std::size_t vdp) {
    const std::size_t fifo = state[vdp + kVdpFifoSizeAt];
    return vdp + kVdpFifoSizeAt + 1 + 12 * fifo;
}

std::size_t vdpSpriteCounters(const std::vector<std::uint8_t>& state, std::size_t vdp) {
    return vdpAfterFifo(state, vdp) + kVdpAfterFifo;
}

}  // namespace

TEST_CASE("a state whose VDP sprite counters index past their tables is refused", "[blastem][saves]") {
    // The four counters are loaded raw: the draw list is MAX_SPRITES_LINE entries inside the VDP context, and an index
    // past it is a write into the neighbouring fields (which a sanitizer cannot see, because it is inside one struct).
    // Each of the three counters is held to the range the renderer itself produces: sprite_draws to the line's sprites,
    // slot_counter to the sprite-info slots, cur_slot to the draw list plus its -1 empty value.
    auto sys = boot(rp::blastem::test::vintDmaRom());
    run(*sys, 2);
    const std::vector<std::uint8_t> base    = sys->saveStateBytes();
    const std::size_t               vdp     = sectionPayload(base, kSectionVdp);
    REQUIRE(vdp != kNoSection);
    const std::size_t counters = vdpSpriteCounters(base, vdp);
    REQUIRE(base[counters + 1] <= 20);  // MAX_SPRITES_LINE, so the offsets above are the fields they claim to be
    // The four counters: sprite_index has no value above 0x80 (the "no more sprites" marker), and the other three have
    // no value above the line's 20.
    const std::uint8_t bad[4] = {0xFF, 21, 21, 21};
    for (std::size_t field = 0; field < 4; ++field) {
        std::vector<std::uint8_t> state = base;
        state[counters + field]         = bad[field];
        auto restored                   = boot(rp::blastem::test::vintDmaRom(), state);
        CHECK(restored->stateRefused());
        CHECK(restored->activated());
    }
    // And the values a running VDP really holds are not refused: 0x80 is the marker the sprite passes write to mean "no
    // more", and 80 to 127 are the link values a stop can fall on between the SAT read and the clamp that follows it.
    // The review's proposed ceiling of 80 for this field would have refused a state saved mid-scan (and did, for the
    // DMA image the review's own fuzzer reached: it saves with 128 there).
    for (std::uint8_t live : {std::uint8_t(0x80), std::uint8_t(100)}) {
        std::vector<std::uint8_t> state = base;
        state[counters]                 = live;
        auto restored                   = boot(rp::blastem::test::vintDmaRom(), state);
        CHECK_FALSE(restored->stateRefused());
        CHECK(restored->activated());
    }
}

namespace {

// The energy in a render: what tells a note that kept playing from one that went silent on the restore. A restored
// FM note's operators can all keep the power-on phase increment, which is silence with a click at the load; the peak
// alone cannot see that (the click clears the audibility check the round trip uses).
double rms(const std::vector<float>& samples) {
    double sum = 0.0;
    for (float s : samples) sum += double(s) * double(s);
    return samples.empty() ? 0.0 : std::sqrt(sum / double(samples.size()));
}

}  // namespace

TEST_CASE("a restored FM note plays the note it was saved in", "[blastem][saves]") {
    // fmOnlyRom is the image that really plays the YM2612 (the other one's writes never land without the Z80's bus), and
    // it is the case the review needed: ym_deserialize replays the register writes before it loads block and fnum, and
    // the write that computes the operators' phase increments is the one it skips, so every operator kept the power-on
    // increment and the restored note was silent (RMS 0.002 against the original's 0.118). The mirror recomputes them
    // after the load; a rate comparison is what a sink can honestly check, because the grid phase is the host's and a
    // restored render is not sample-identical to the original's continuation.
    auto sys = boot(rp::blastem::test::fmOnlyRom());
    run(*sys, 20);  // the note is sounding
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::vector<float>        original = run(*sys, 20);
    auto restored = boot(rp::blastem::test::fmOnlyRom(), state);
    REQUIRE_FALSE(restored->stateRefused());
    REQUIRE(restored->activated());
    const std::vector<float> restoredAudio = run(*restored, 20);
    const double             before        = rms(original);
    REQUIRE(before > 0.05);  // the original is playing, or the comparison proves nothing
    CHECK(rms(restoredAudio) > 0.5 * before);
}

TEST_CASE("a state without the console's mandatory sections is refused", "[blastem][saves]") {
    // The host's walk checked framing only, so a state holding one section (the identifier and the PSG) or every
    // section but the VDP loaded with the rest of the console at power-on, and stateRefused() said nothing was wrong:
    // the review's only-ident-plus-PSG and drop-VDP reproducers. A half-loaded core is not a machine in any state the
    // caller asked for, so the walk requires the console's own section set (genesis_serialize's order: 68000, Z80,
    // VDP, YM2612, PSG, the bus arbiter, the three IO ports, main RAM and sound RAM) exactly once each.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    const std::vector<RawSection> whole = splitSections(sys->saveStateBytes());
    REQUIRE(whole.size() > 5);

    std::vector<RawSection> onlyPsg;
    for (const RawSection& s : whole) {
        if (s.id == kSectionPsg) onlyPsg.push_back(s);
    }
    auto partial = boot(rp::blastem::test::fmPsgRom(), assembleSections(onlyPsg));
    CHECK(partial->stateRefused());
    CHECK(partial->activated());

    std::vector<RawSection> noVdp;
    for (const RawSection& s : whole) {
        if (s.id != kSectionVdp) noVdp.push_back(s);
    }
    auto missing = boot(rp::blastem::test::fmPsgRom(), assembleSections(noVdp));
    CHECK(missing->stateRefused());
    CHECK(missing->activated());
    CHECK(peak(run(*missing, 4)) > 0.01);  // the power-on machine
}

TEST_CASE("a state with a repeated or unknown section is refused", "[blastem][saves]") {
    // A section is one piece of the console, so a second one of the same id is not a state BlastEm's own walk can
    // produce, and neither is a section no handler in this build reads: both used to be skipped with a warning and the
    // rest of the state accepted. The review's relabel reproducer renamed the 68000 section, which then left the 68k at
    // power-on.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    const std::vector<RawSection> whole = splitSections(sys->saveStateBytes());
    REQUIRE(whole.size() > 5);

    std::vector<RawSection> repeated = whole;
    repeated.push_back(repeated[1]);  // a second 68000 section
    auto dup = boot(rp::blastem::test::fmPsgRom(), assembleSections(repeated));
    CHECK(dup->stateRefused());
    CHECK(dup->activated());

    std::vector<RawSection> relabelled = whole;
    relabelled[0].id                   = 0x8000;  // no section this build knows
    auto unknown                       = boot(rp::blastem::test::fmPsgRom(), assembleSections(relabelled));
    CHECK(unknown->stateRefused());
    CHECK(unknown->activated());
}

TEST_CASE("a state whose component clocks are far from the 68k's is refused", "[blastem][saves]") {
    // Every component's cycle count is an offset from the same cycle_base and the machine runs one clock: a sync runs
    // each of them to the 68k's count. A state that moves one count far from the others is not a machine this core
    // writes, and running it makes the components catch up across the gap inside the next block, on the block thread:
    // the review's reproducer changed only the 68k's count to 0xF0000000 and the first blocks took 1.8 s instead of 5 ms.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state     = sys->saveStateBytes();
    const std::size_t         m68k      = sectionPayload(state, kSection68000);
    REQUIRE(m68k != kNoSection);
    // m68k_serialize writes eight data registers, eight address registers, the other stack pointer, the pc and the status
    // register before the cycle count.
    const std::size_t cycles = m68k + 8 * 4 + 8 * 4 + 4 + 4 + 2;
    state[cycles] = 0xF0;
    state[cycles + 1] = 0;
    state[cycles + 2] = 0;
    state[cycles + 3] = 0;
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);
}

TEST_CASE("a state saved after a 64K-word DMA is still accepted", "[blastem][saves]") {
    // The one gap the clocks legitimately have: a 68k-to-VDP DMA moves the 68k's count to the transfer's end while the
    // sound chips wait for the block-end sync, and the longest transfer is a fifth of a second. A bound that refused
    // this would refuse a real ROM's state after every big DMA.
    auto sys = boot(rp::blastem::test::dmaLoopRom(false));
    run(*sys, 8);  // several transfers, so the state is saved with the gap
    auto restored = boot(rp::blastem::test::dmaLoopRom(false), sys->saveStateBytes());
    CHECK_FALSE(restored->stateRefused());
    CHECK(restored->activated());
}

namespace {

// A cart with 64 KiB of odd-byte SRAM (as genmddj's header declares it) and a program that writes one byte into it at
// $200001 and reads it back into work RAM: the battery a Duplicate has to carry, and the ROM's own proof of it.
std::vector<std::uint8_t> batteryRom() {
    std::vector<std::uint8_t> rom = rp::blastem::test::image(
        {0x13, 0xFC, 0x00, 0x5A, 0x00, 0x20, 0x00, 0x01,  // move.b #$5A,$00200001
         0x10, 0x39, 0x00, 0x20, 0x00, 0x01,              // move.b $00200001,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00},             // move.b d0,$FF0000
        {});
    rom[0x1B0] = 'R';
    rom[0x1B1] = 'A';
    rom[0x1B2] = 0xF8;  // SRAM present, odd bytes
    rom[0x1B3] = 0x20;
    rom[0x1B4] = 0x00; rom[0x1B5] = 0x20; rom[0x1B6] = 0x00; rom[0x1B7] = 0x01;  // start $00200001
    rom[0x1B8] = 0x00; rom[0x1B9] = 0x21; rom[0x1BA] = 0xFF; rom[0x1BB] = 0xFF;  // end $0021FFFF
    return rom;
}

}  // namespace

TEST_CASE("a duplicated battery cart keeps its battery", "[blastem][saves]") {
    // BlastEm's serializer has no SRAM section (only a mapper cart's registers), but the host's state is what a Duplicate
    // copies (systemsStore.ts pulls the source's savestate and gives the copy a fresh .sav path), so a state that does
    // not carry the battery duplicates a game and loses its save. This host appends the battery in the .sav's order as
    // its own section, after BlastEm's, and restores it on a load.
    auto sys = boot(batteryRom());
    run(*sys, 4);  // the ROM writes $5A into the battery
    REQUIRE(sys->saveSramBytes().size() == 0x10000);
    REQUIRE(sys->saveSramBytes()[0] == 0x5A);
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    // A fresh instance with the state and NO battery seed: the battery can only come out of the state.
    auto duplicate = boot(batteryRom(), state);
    REQUIRE_FALSE(duplicate->stateRefused());
    REQUIRE(duplicate->activated());
    REQUIRE(duplicate->saveSramBytes().size() == 0x10000);
    CHECK(duplicate->saveSramBytes()[0] == 0x5A);
    CHECK(ramByte(*duplicate) == 0x5A);  // the ROM read it back, so the restored battery is the one its program saw
}

TEST_CASE("a state from another region's console is refused", "[blastem][saves]") {
    // The console's region is the VDP's own flags (FLAG2_REGION_PAL, the HZ50 bit genesis.c sets at set_region), and a
    // state does not carry the machine's 224 or 240-line frame: loading a PAL state into an NTSC instance, or the other
    // way round, left an incoherent machine (a 224-line frame with the PAL flag set, the review's reproducer) rather
    // than the one the build asked for. The instance's region is the boot core's own flag, so the two must agree.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         vdp   = sectionPayload(state, kSectionVdp);
    REQUIRE(vdp != kNoSection);
    const std::size_t flags = vdpAfterFifo(state, vdp);
    REQUIRE(flags + 1 < state.size());
    state[flags] |= 0x10;  // FLAG2_REGION_PAL in the flags' high byte, so the state says PAL and this core is NTSC
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
}

TEST_CASE("a state from a newer VDP section is refused", "[blastem][saves]") {
    // The VDP section's version byte says which layout follows it, and a newer build's layout is not this one: the
    // fields after it may be longer, shorter or moved. The mirror warned and read on, so a state from a newer BlastEm
    // behind the same identifier was misread; the section's fields are a user file's and the build can only refuse it.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    const std::size_t         vdp   = sectionPayload(state, kSectionVdp);
    REQUIRE(vdp != kNoSection);
    state[vdp] = 6;  // VDP_STATE_VERSION is 5 in this build
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
    CHECK(restored->activated());
}

TEST_CASE("a published snapshot is the state the control plane saves", "[blastem][saves]") {
    // The block thread's capture reuses its buffers (BlastEm's serializer buffer, the battery's scratch and the
    // destination vector) so a publish does not allocate once it has run a few times, and this is the check that the
    // reuse keeps producing the same container the control plane's saveStateBytes() hands out: a publish and a save
    // with nothing run between them are the same bytes, before and after the buffers have been reused.
    auto sys = boot(batteryRom());
    run(*sys, 4);
    REQUIRE(sys->enableStateSnapshot());
    std::vector<std::uint8_t> slot(sys->stateSnapshotCapacity() + 4);
    for (int round = 0; round < 3; ++round) {
        sys->publishStateSnapshot(24000, kRate);  // past the publish interval, so a capture happens
        const std::size_t len = sys->readStateSnapshot(slot.data(), slot.size());
        REQUIRE(len > 8);
        const std::vector<std::uint8_t> published(slot.begin() + 4, slot.begin() + 4 + static_cast<std::ptrdiff_t>(len));
        const std::vector<std::uint8_t> saved = sys->saveStateBytes();
        CHECK(published == saved);
        run(*sys, 1);  // move on, so the next publish is a different state and the buffers are reused for it
    }
}
