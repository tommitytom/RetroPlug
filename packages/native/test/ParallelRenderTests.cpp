// Determinism + thread-safety tests for the offline parallel renderer
// (system/OfflineRender.cpp). The contract is byte-identity: rendering a project
// across worker threads (one task per render unit) must produce exactly the same
// per-system audio as a single-threaded runBlock + PerSystemRouter sequence over
// the same starting state. These also double as the real worker-thread safety
// gate under ThreadSanitizer (tools/run-sanitizers.sh thread) — disjoint per-slot
// buffers + intra-unit-only serial ferry should leave it clean.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "project/Project.hpp"
#include "system/BlockRunner.hpp"
#include "system/OfflineRender.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "transport/MidiTypes.hpp"

#ifndef RETROPLUG_TEST_GB_ROM
#  error "RETROPLUG_TEST_GB_ROM must be defined by CMake"
#endif
#ifndef RETROPLUG_TEST_ROM_DIR
#  error "RETROPLUG_TEST_ROM_DIR must be defined by CMake"
#endif

namespace {

constexpr double kSampleRate = 44100.0;
const std::string kGbRomPath  = RETROPLUG_TEST_GB_ROM;
const std::string kNesRomPath = std::string(RETROPLUG_TEST_ROM_DIR) + "/n8-midi.nes";

std::vector<std::uint8_t> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

bool exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

OfflineRenderParams makeParams(std::uint64_t totalSamples, std::uint32_t blockSize) {
    OfflineRenderParams p{};
    p.totalSamples     = totalSamples;
    p.blockSize        = blockSize;
    p.sampleRate       = kSampleRate;
    p.bpm              = 120.0;
    p.transportPlaying = true;   // exercise the per-block ppq accumulation
    p.startPpq         = 0.0;
    return p;
}

// The single-threaded reference: the shipped runBlock + PerSystemRouter loop
// (exactly what TestHarness::runMsPerSystem does, minus capture draining). Ground
// truth the parallel renderer must match byte-for-byte.
std::vector<std::vector<float>> renderSingleThreaded(Project& project,
                                                     const OfflineRenderParams& p) {
    auto& systems = project.systems();
    const std::size_t n = systems.size();
    std::vector<std::vector<float>> out(n);
    for (auto& v : out) v.reserve(static_cast<std::size_t>(p.totalSamples) * 2);

    std::vector<std::vector<float>> bl(n, std::vector<float>(p.blockSize));
    std::vector<std::vector<float>> br(n, std::vector<float>(p.blockSize));
    std::vector<float*> ls(n), rs(n);
    for (std::size_t i = 0; i < n; ++i) { ls[i] = bl[i].data(); rs[i] = br[i].data(); }
    PerSystemRouter router(ls.data(), rs.data());

    double ppq = p.startPpq;
    for (std::uint64_t s = 0; s < p.totalSamples; s += p.blockSize) {
        const std::uint32_t frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(p.blockSize, p.totalSamples - s));
        const AudioBlockInfo info{ frames, p.sampleRate, p.bpm, ppq, p.transportPlaying };
        for (std::size_t i = 0; i < n; ++i) {
            std::fill_n(ls[i], frames, 0.0f);
            std::fill_n(rs[i], frames, 0.0f);
        }
        runBlock(info, project, router);
        for (std::size_t i = 0; i < n; ++i)
            for (std::uint32_t f = 0; f < frames; ++f) {
                out[i].push_back(ls[i][f]);
                out[i].push_back(rs[i][f]);
            }
        if (p.transportPlaying)
            ppq += (p.bpm / 60.0) * (static_cast<double>(frames) / p.sampleRate);
    }
    return out;
}

void expectByteIdentical(const std::vector<std::vector<float>>& parallel,
                         const std::vector<std::vector<float>>& single) {
    REQUIRE(parallel.size() == single.size());
    for (std::size_t s = 0; s < parallel.size(); ++s) {
        REQUIRE(parallel[s].size() == single[s].size());
        const bool eq = std::equal(parallel[s].begin(), parallel[s].end(), single[s].begin());
        CHECK(eq);
    }
}

// A real-audio guard: byte-identity over two all-zero buffers proves nothing, so
// every determinism case asserts its per-system reference actually has signal.
bool hasAudio(const std::vector<float>& buf) {
    return std::any_of(buf.begin(), buf.end(), [](float x) { return x != 0.0f; });
}

// `count` SameBoy instances. If `linkGroupId != 0`, the first two share it (a
// 2-member link group); the rest are standalone.
std::unique_ptr<Project> makeSameBoyProject(const std::vector<std::uint8_t>& rom,
                                            int count, std::uint8_t linkGroupId) {
    auto project = std::make_unique<Project>();
    for (int i = 0; i < count; ++i) {
        SameBoyConfig cfg{};
        cfg.romPath = kGbRomPath;
        if (linkGroupId != 0 && i < 2) cfg.linkGroupId = linkGroupId;
        // Distinct per-slot gain so each system's output is DISTINGUISHABLE. With
        // identical config every system boots byte-identical, and a slot-mapping
        // cross-wire in the parallel renderer would land audio in the wrong slot
        // yet still byte-match — invisible. Distinct gains make a cross-wire diverge
        // (both paths apply the same per-slot gain via the same routing, so a
        // CORRECT render still byte-matches).
        cfg.gainDb = -3.0f * static_cast<float>(i);
        const SystemId id = project->nextSystemId();
        auto sys = std::make_unique<SameBoySystem>(id, cfg, rom);
        sys->onActivate(kSampleRate);
        project->adoptSystem(sys.release());
    }
    project->rebuildLinkGroups();
    return project;
}

std::unique_ptr<Project> makeNesProject(const std::vector<std::uint8_t>& rom, int count) {
    auto project = std::make_unique<Project>();
    for (int i = 0; i < count; ++i) {
        MesenNesConfig cfg{};
        cfg.romPath = kNesRomPath;
        const SystemId id = project->nextSystemId();
        auto sys = std::make_unique<MesenNesSystem>(id, cfg, rom);
        sys->onActivate(kSampleRate);
        project->adoptSystem(sys.release());
    }
    project->rebuildLinkGroups();
    return project;
}

// n8-midi.nes (the evermidi ROM) is SILENT until driven by MIDI — comparing two
// silent renders proves nothing. Settle the ROM (~1s) so its main loop is
// servicing the everdrive FIFO, then sustain a note on every instance. MIDI ch1
// (status 0x90) -> APU Pulse 1 (a built-in voice Mesen emulates), note 60 is in
// range -> real, deterministic audio for the byte-identity comparison. The warmup
// runs on the calling (main) thread, so the subsequent parallel render exercises
// the main->worker IsEmulationThread() rebind.
void primeNes(Project& p) {
    renderSingleThreaded(p, makeParams(/*~1s settle*/ 44100, /*blockSize*/ 256)); // discard
    ::MidiEvent ev{};
    ev.frame = 0; ev.size = 3;
    ev.data[0] = 0x90; ev.data[1] = 60; ev.data[2] = 100;   // ch1 NoteOn, note 60
    for (auto& s : p.systems()) if (s) s->onMidi(&ev, 1);
}

} // namespace

TEST_CASE("Parallel render byte-matches single-threaded: 3 standalone SameBoys",
          "[OfflineRender][determinism]") {
    if (!exists(kGbRomPath)) SKIP("Game Boy ROM missing at " << kGbRomPath);
    const auto rom = loadFile(kGbRomPath);

    // 8200 is deliberately NOT a multiple of the 256 block size, so the partial
    // final block (8 frames) is exercised by a byte-identity comparison.
    const auto params = makeParams(/*totalSamples*/ 8200, /*blockSize*/ 256);
    auto single   = makeSameBoyProject(rom, 3, /*linkGroupId*/ 0);
    auto parallel = makeSameBoyProject(rom, 3, /*linkGroupId*/ 0);

    const auto refOut = renderSingleThreaded(*single, params);
    const auto parOut = renderUnitsParallel(*parallel, params);

    // Three distinct units (one task each), each with a distinct gain -> distinct,
    // non-silent audio per slot (so a routing cross-wire would diverge).
    REQUIRE(refOut.size() == 3);
    CHECK(hasAudio(refOut[0]));
    CHECK(hasAudio(refOut[1]));
    CHECK(hasAudio(refOut[2]));
    CHECK(refOut[0] != refOut[1]);   // distinct gains -> distinguishable slots
    expectByteIdentical(parOut, refOut);
}

TEST_CASE("Parallel render byte-matches single-threaded: 2 standalone + a link group",
          "[OfflineRender][determinism]") {
    if (!exists(kGbRomPath)) SKIP("Game Boy ROM missing at " << kGbRomPath);
    const auto rom = loadFile(kGbRomPath);

    const auto params = makeParams(/*totalSamples*/ 8192, /*blockSize*/ 256);
    // 4 systems: slots 0,1 are a 2-member link group (one unit); slots 2,3 are
    // standalone (two more units). Exercises a multi-member unit on a worker.
    auto single   = makeSameBoyProject(rom, 4, /*linkGroupId*/ 1);
    auto parallel = makeSameBoyProject(rom, 4, /*linkGroupId*/ 1);

    REQUIRE(single->linkGroups().size() == 1);
    REQUIRE(single->linkGroups().front().size() == 2);

    const auto refOut = renderSingleThreaded(*single, params);
    const auto parOut = renderUnitsParallel(*parallel, params);

    REQUIRE(refOut.size() == 4);
    for (const auto& slot : refOut) CHECK(hasAudio(slot));   // every slot has signal
    expectByteIdentical(parOut, refOut);
}

TEST_CASE("Parallel render byte-matches single-threaded: 2 Mesen NES instances",
          "[OfflineRender][determinism][mesen]") {
    if (!exists(kNesRomPath)) SKIP("NES ROM missing at " << kNesRomPath);
    const auto rom = loadFile(kNesRomPath);
    REQUIRE(rom.size() > 16);

    // Two Mesen units rendered on two worker threads — the concurrent-Mesen
    // determinism + safety case (also clean under TSan). primeNes() drives a
    // sustained APU note into both instances FIRST, so this compares real audio,
    // not silence: a mis-stepped / wrongly-rebound instance would diverge here.
    auto single   = makeNesProject(rom, 2);
    auto parallel = makeNesProject(rom, 2);
    primeNes(*single);
    primeNes(*parallel);

    // 22050 (~0.5 s) is not a multiple of 256 -> also covers the partial tail.
    const auto params = makeParams(/*totalSamples*/ 22050, /*blockSize*/ 256);
    const auto refOut = renderSingleThreaded(*single, params);
    const auto parOut = renderUnitsParallel(*parallel, params);

    REQUIRE(refOut.size() == 2);
    CHECK(hasAudio(refOut[0]));   // the note actually sounded — not silence-vs-silence
    CHECK(hasAudio(refOut[1]));
    expectByteIdentical(parOut, refOut);
}
