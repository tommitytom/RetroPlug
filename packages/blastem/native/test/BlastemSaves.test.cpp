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

// serialize.h's section ids, for the two cases that reach one field of a real state without assuming the layout of the
// rest of it.
constexpr std::size_t kSectionMainRam = 10;   // SECTION_MAIN_RAM
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
