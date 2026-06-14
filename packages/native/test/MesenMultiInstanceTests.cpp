// Smoke-test the Mesen singletons via two real MesenNesSystem instances on a
// real NES ROM. If FolderUtilities / MessageManager / GameDatabase are
// genuinely hostile to multi-instance use we expect to see it here: crash,
// hang, or zero audio out of one of the two systems.
//
// Paired with MesenSingletonsTests.cpp which probes each singleton in
// isolation; this test proves the singleton path is actually exercised by
// the activation flow at MesenNesSystem.cpp:80-135.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "Core/Shared/MessageManager.h"
#include "Utilities/FolderUtilities.h"

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"

#ifndef RETROPLUG_TEST_ROM_DIR
#  error "RETROPLUG_TEST_ROM_DIR must be defined by CMake — points at resources/roms"
#endif

namespace {

std::vector<std::uint8_t> loadRom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    f.seekg(0, std::ios::end);
    const auto len = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(len));
    f.read(reinterpret_cast<char*>(bytes.data()), len);
    return bytes;
}

} // namespace

TEST_CASE("Two MesenNesSystem instances boot side-by-side and stay isolated",
          "[MesenMultiInstance]") {
    const std::string romPath =
        std::string(RETROPLUG_TEST_ROM_DIR) + "/n8-midi.nes";
    auto romBytes = loadRom(romPath);
    REQUIRE(romBytes.size() > 16); // at least a valid iNES header

    constexpr double        kSampleRate = 44100.0;
    constexpr std::uint32_t kBlockSize  = 256;
    constexpr int           kBlocks     = 10;

    MesenNesConfig cfgA{};
    cfgA.romPath = romPath;
    cfgA.gainDb  = 0.0f;
    MesenNesConfig cfgB = cfgA;

    MesenNesSystem sysA{SystemId{0}, cfgA, romBytes};
    MesenNesSystem sysB{SystemId{1}, cfgB, romBytes};

    sysA.onActivate(kSampleRate);
    sysB.onActivate(kSampleRate);

    // Per-instance L/R buffers. onProcess SUMs into outs (see SystemBase
    // contract at SystemBase.hpp:38) so we zero between calls.
    std::vector<float> aL(kBlockSize), aR(kBlockSize);
    std::vector<float> bL(kBlockSize), bR(kBlockSize);
    float* aOuts[2] = { aL.data(), aR.data() };
    float* bOuts[2] = { bL.data(), bR.data() };

    AudioBlockInfo info{};
    info.frames           = kBlockSize;
    info.sampleRate       = kSampleRate;
    info.tempo            = 120.0;
    info.ppqPosBlockStart = 0.0;
    info.transportPlaying = false;

    std::vector<float> aMix, bMix;
    aMix.reserve(kBlockSize * 2 * kBlocks);
    bMix.reserve(kBlockSize * 2 * kBlocks);

    for (int blk = 0; blk < kBlocks; ++blk) {
        std::fill(aL.begin(), aL.end(), 0.0f);
        std::fill(aR.begin(), aR.end(), 0.0f);
        std::fill(bL.begin(), bL.end(), 0.0f);
        std::fill(bR.begin(), bR.end(), 0.0f);

        sysA.onProcess(info, aOuts);
        sysB.onProcess(info, bOuts);

        aMix.insert(aMix.end(), aL.begin(), aL.end());
        aMix.insert(aMix.end(), aR.begin(), aR.end());
        bMix.insert(bMix.end(), bL.begin(), bL.end());
        bMix.insert(bMix.end(), bR.begin(), bR.end());

        info.ppqPosBlockStart += (static_cast<double>(kBlockSize) *
                                  info.tempo / 60.0) / kSampleRate;
    }

    // Two clones of the same ROM, booted from the same starting state, should
    // be sample-identical. If any singleton in Mesen leaked state from one
    // instance into the other (FolderUtilities config divergence, shared
    // SoundMixer accumulator, GameDatabase corruption mid-load) we'd see the
    // two streams diverge here. n8-midi.nes is silent without MIDI input, so
    // the audio buffers themselves may be all-zero — that's fine; what we're
    // testing is per-instance isolation, not whether the ROM happens to make
    // noise.
    REQUIRE(aMix.size() == bMix.size());
    std::size_t firstDiff = aMix.size();
    for (std::size_t i = 0; i < aMix.size(); ++i) {
        if (aMix[i] != bMix[i]) { firstDiff = i; break; }
    }
    CHECK(firstDiff == aMix.size());
    if (firstDiff != aMix.size()) {
        WARN("MesenNesSystem outputs diverged at sample " << firstDiff
             << " — possible singleton-mediated state leak");
    }

    // FolderUtilities was set by both onActivate calls. Both wrote the same
    // path, so observable state is the shared default. The key thing is
    // that GetHomeFolder doesn't throw — meaning the home folder slot is in
    // a usable state for any later Mesen code that needs it.
    CHECK(FolderUtilities::GetHomeFolder() == "/tmp/retroplug-mesen");

    // Mesen logs during boot (ROM load, mapper init, etc.). The shared log
    // proves the MessageManager singleton was exercised by both instances —
    // any singleton-related crash on the Log() path would surface here.
    CHECK(!MessageManager::GetLog().empty());

    sysA.onDeactivate();
    sysB.onDeactivate();
}
