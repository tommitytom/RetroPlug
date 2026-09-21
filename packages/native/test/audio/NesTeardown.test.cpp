// The NES twin of SmsAudio's construct/destruct case.
//
// Mesen declares Emulator::_console before _soundMixer and leaves ~Emulator empty, so members destruct in
// reverse order and the mixer is gone by the time the console's audio providers reach for it. SMS found
// this the hard way - SmsFmAudio registers with the mixer in its constructor and unregisters in its
// destructor, unconditionally, and ~3 in 5 runs of 40 cycles segfaulted before onDeactivate learned to
// call Emulator::Stop first.
//
// NES has the identical shape, only CONDITIONALLY: Core/NES/Epsm.cpp registers in its constructor and
// unregisters in its destructor, and BaseMapper constructs it whenever a cart's NES 2.0 header sets
// HasEpsm. That is why the fix landed on SMS alone and why nobody saw the NES half - it needs a
// particular kind of cart.
//
// Which is also the honest limit of this test. The vendored bliptoaster ROM has no EPSM bit, so this
// cannot reproduce the specific crash; what it guards is that the shared teardown path stays sound under
// repetition, the same way the SMS case does. Reproducing the EPSM crash would need an EPSM cart in the
// tree, and ASan cannot help either - both frames live in the uninstrumented libmesen.a.
//
// Run via `pnpm test:plugin`.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 256;

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_BLIPTOASTER_ROM_PATH, "rb");
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

std::unique_ptr<MesenNesSystem> build(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto sys = std::make_unique<MesenNesSystem>(id, cfg, readRom());
    sys->onActivate(kSampleRate);
    return sys;
}

/** One block through the triad, so the APU is genuinely live before teardown. */
void runBlock(MesenNesSystem& sys) {
    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    float* outs[2] = { l.data(), r.data() };
    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, outs, 2);
}

} // namespace

TEST_CASE("NES construct/destruct cycles do not crash", "[nes]") {
    // The repetition IS the test: the SMS failure it mirrors was nondeterministic, so a single cycle
    // proved nothing there either.
    for (int i = 0; i < 40; ++i) {
        auto sys = build(static_cast<SystemId>(i + 1));
        REQUIRE(sys->activated());
        runBlock(*sys);
        sys.reset();
    }
    SUCCEED("40 construct/render/destruct cycles survived");
}
