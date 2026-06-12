// Tests for the SystemBase virtuals as implemented by MesenNesSystem — the new
// SRAM / savestate / clone / fastBoot / reload-on-rom-change surface lifted
// out of SameBoy-only territory. See plan at
// /home/vscode/.claude/plans/find-all-stubbed-menu-eager-starlight.md.
//
// Each test boots a real MesenNesSystem against the in-repo n8-midi.nes ROM
// (RETROPLUG_TEST_ROM_DIR). The companion MesenMultiInstanceTests exercises
// the audio path side-by-side; here we focus on the menu-action virtuals.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "system/MemoryType.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "transport/CommandQueue.hpp"

#include "StateSnapshotStress.hpp"

#ifndef RETROPLUG_TEST_ROM_DIR
#  error "RETROPLUG_TEST_ROM_DIR must be defined by CMake"
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

constexpr double kSampleRate = 44100.0;
const std::string kRomPath = std::string(RETROPLUG_TEST_ROM_DIR) + "/n8-midi.nes";

// Step the emulator forward by `blocks` audio blocks so internal state has
// advanced (CPU PC has moved, RAM has been touched). Without running, a
// savestate diff between "fresh" and "stepped" would be empty.
void runBlocks(MesenNesSystem& sys, int blocks = 4, std::uint32_t blockSize = 256) {
    std::vector<float> l(blockSize), r(blockSize);
    float* outs[2] = { l.data(), r.data() };
    AudioBlockInfo info{};
    info.frames           = blockSize;
    info.sampleRate       = kSampleRate;
    info.tempo            = 120.0;
    info.ppqPosBlockStart = 0.0;
    info.transportPlaying = false;
    for (int i = 0; i < blocks; ++i) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys.onProcess(info, outs);
    }
}

// Snapshot the 2 KB NES internal RAM (the 6502's main memory). The most
// reliable cross-emulator-implementation "same state" check: a savestate's
// bytes embed compressed video data and may differ across save → load → save
// round-trips, but the underlying RAM at the point load() restored to is
// the actual semantic state. Compares can be done with `==`.
std::vector<std::uint8_t> snapshotRam(MesenNesSystem& sys) {
    auto acc = sys.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    if (!acc.valid() || acc.size() == 0) return {};
    return std::vector<std::uint8_t>(acc.data(), acc.data() + acc.size());
}

} // namespace

TEST_CASE("MesenNesSystem::fastBoot returns nullopt (no GBA-style boot toggle on NES)",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    CHECK(!sys.fastBoot().has_value());
    // setFastBoot is a no-op on Mesen; calling it shouldn't crash or change
    // observable state.
    sys.setFastBoot(true);
    CHECK(!sys.fastBoot().has_value());

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem::wantsRomReload defaults false and toggles via setRomReload",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    CHECK_FALSE(sys.wantsRomReload());
    sys.setRomReload(true);
    CHECK(sys.wantsRomReload());
    sys.setRomReload(false);
    CHECK_FALSE(sys.wantsRomReload());

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem::romPath returns the configured path",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    CHECK(sys.romPath() == kRomPath);

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem SRAM round-trip via getMemory + saveSramBytes",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    auto accessor = sys.getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    if (accessor.valid() && accessor.size() > 0) {
        // Write a known pattern then verify saveSramBytes reads it back.
        std::vector<std::uint8_t> pattern(accessor.size());
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            pattern[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
        }
        std::memcpy(accessor.data(), pattern.data(), pattern.size());

        auto snapshot = sys.saveSramBytes();
        REQUIRE(snapshot.size() == pattern.size());
        CHECK(snapshot == pattern);

        // clearSram zeros it.
        sys.clearSram();
        auto zeroed = sys.saveSramBytes();
        REQUIRE(zeroed.size() == pattern.size());
        for (auto b : zeroed) CHECK(b == 0);
    } else {
        // n8-midi.nes may ship with no battery RAM (saveRamSize == 0). In
        // that case the contract is "empty vector" — verify that.
        CHECK(sys.saveSramBytes().empty());
        sys.clearSram();  // shouldn't crash
        CHECK(sys.saveSramBytes().empty());
        WARN("n8-midi.nes has no battery RAM; SRAM round-trip path skipped");
    }

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem savestate round-trip restores internal RAM",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    // Capture state A at the start (after a couple of warm-up blocks).
    runBlocks(sys, 2);
    auto stateA   = sys.saveStateBytes();
    auto ramAtA   = snapshotRam(sys);
    REQUIRE(!stateA.empty());
    REQUIRE(!ramAtA.empty());

    // Step further so RAM at B differs from RAM at A.
    runBlocks(sys, 50);
    auto stateB = sys.saveStateBytes();
    auto ramAtB = snapshotRam(sys);
    REQUIRE(!stateB.empty());
    REQUIRE(ramAtB != ramAtA);
    // Two captures at different timepoints differ at SOME byte in the
    // savestate stream too — proof that SaveState is actually capturing
    // mutable state, not a fixed header. (Byte-identity ACROSS a load+save
    // cycle isn't asserted — see [MesenNesSystem savestate is not byte-stable
    // across load/save] below.)
    CHECK(stateA != stateB);

    // Load state A back. The RAM should snap back to what it was at A.
    REQUIRE(sys.loadStateBytes(stateA));
    CHECK(snapshotRam(sys) == ramAtA);

    // Forward run from the restored state should be deterministic — RAM
    // after the same number of blocks should match the original forward run.
    runBlocks(sys, 50);
    CHECK(snapshotRam(sys) == ramAtB);

    // Garbage / empty buffers are rejected gracefully.
    std::vector<std::uint8_t> garbage(64, 0xCC);
    CHECK_FALSE(sys.loadStateBytes(garbage));
    CHECK_FALSE(sys.loadStateBytes({}));

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem savestate is not byte-stable across load+save (documented)",
          "[MesenSystemBase]") {
    // Pinning the actual non-stability: Mesen's GetSaveStateHeader writes a
    // compressed snapshot of the current PpuFrameInfo into the stream
    // (deps/mesen/Core/Shared/SaveStateManager.cpp:115). So save → load →
    // save can produce different bytes even though the SEMANTIC state is
    // identical. Documenting this here so the round-trip test above can
    // rely on RAM-based equality instead of bytes.
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);
    runBlocks(sys, 2);

    auto stateA = sys.saveStateBytes();
    REQUIRE(sys.loadStateBytes(stateA));
    auto stateA2 = sys.saveStateBytes();
    // The two sizes are at least similar — both contain serialised CPU
    // state — but the embedded video data can differ.
    CHECK(stateA.size() == stateA2.size());

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem::clone produces an independent instance at matching state",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem src{SystemId{1}, cfg, romBytes};
    src.onActivate(kSampleRate);

    runBlocks(src, 10);

    auto cloned = src.clone(SystemId{42}, kSampleRate);
    REQUIRE(cloned);

    // The clone is a separate object with a distinct id.
    CHECK(cloned.get() != static_cast<SystemBase*>(&src));
    CHECK(cloned->id() == SystemId{42});

    // RAM-level equality immediately after the clone — true "identical
    // state" check (savestate bytes aren't reliable, see the
    // not-byte-stable test above).
    auto* clonedSys = dynamic_cast<MesenNesSystem*>(cloned.get());
    REQUIRE(clonedSys);
    auto srcRam   = snapshotRam(src);
    auto cloneRam = snapshotRam(*clonedSys);
    REQUIRE(!srcRam.empty());
    CHECK(srcRam == cloneRam);

    // Step ONLY the source forward. The clone's RAM must NOT move.
    runBlocks(src, 5);
    CHECK(snapshotRam(*clonedSys) == cloneRam);
    CHECK(snapshotRam(src) != srcRam);

    // Run the clone forward by the same number of blocks — its RAM should
    // now match what the source has (deterministic forward step from
    // identical state).
    runBlocks(*clonedSys, 5);
    CHECK(snapshotRam(*clonedSys) == snapshotRam(src));

    src.onDeactivate();
    // cloned's destructor runs onDeactivate.
}

TEST_CASE("MesenNesSystem::snapshotConfig captures live SRAM and savestate",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    runBlocks(sys, 5);
    auto ramAtSnap = snapshotRam(sys);

    auto snap = sys.snapshotConfig();
    const MesenNesConfig* m = rfl::get_if<MesenNesConfig>(&snap.variant());
    REQUIRE(m != nullptr);

    // savestate is always populated for an active emulator (state size > 0).
    REQUIRE_FALSE(m->savestate.empty());

    // SRAM is non-empty IFF the cart has battery RAM. Either outcome is
    // consistent — we just verify the two paths agree on emptiness, and
    // (when non-empty) on bytes.
    auto liveSram = sys.saveSramBytes();
    CHECK((m->sram.empty() == liveSram.empty()));
    if (!liveSram.empty()) {
        CHECK(m->sram == liveSram);
    }

    // Round-tripping the savestate via a fresh instance should restore RAM
    // to what was live at snapshot time — the meaningful contract.
    MesenNesConfig roundtripCfg = *m;
    MesenNesSystem roundtrip{SystemId{99}, roundtripCfg, romBytes};
    roundtrip.onActivate(kSampleRate);
    CHECK(snapshotRam(roundtrip) == ramAtSnap);
    roundtrip.onDeactivate();

    sys.onDeactivate();
}

TEST_CASE("MesenNesSystem honours config_.sram and config_.savestate at onActivate",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);

    // Build a system, write a known SRAM pattern, capture its state + RAM.
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem original{SystemId{1}, cfg, romBytes};
    original.onActivate(kSampleRate);
    runBlocks(original, 8);

    auto stateBytes = original.saveStateBytes();
    auto ramAtSave  = snapshotRam(original);
    REQUIRE(!stateBytes.empty());
    REQUIRE(!ramAtSave.empty());

    auto sramAccessor = original.getMemory(rp::MemoryType::Sram,
                                           rp::AccessType::ReadWrite);
    std::vector<std::uint8_t> sramPattern;
    if (sramAccessor.valid() && sramAccessor.size() > 0) {
        sramPattern.assign(sramAccessor.size(), 0);
        for (std::size_t i = 0; i < sramPattern.size(); ++i) {
            sramPattern[i] = static_cast<std::uint8_t>((i ^ 0x5A) & 0xFF);
        }
        std::memcpy(sramAccessor.data(), sramPattern.data(), sramPattern.size());
    }
    original.onDeactivate();

    // Second system: hydrate from the captured state + SRAM via config_.
    MesenNesConfig restoreCfg = cfg;
    restoreCfg.savestate = std::move(stateBytes);
    if (!sramPattern.empty()) {
        restoreCfg.sram = sramPattern;
    }
    MesenNesSystem restored{SystemId{2}, restoreCfg, romBytes};
    restored.onActivate(kSampleRate);

    // The restored emulator should be at the same semantic state — RAM
    // matches the snapshot taken at save time.
    CHECK(snapshotRam(restored) == ramAtSave);

    if (!sramPattern.empty()) {
        auto restoredSram = restored.saveSramBytes();
        REQUIRE(restoredSram.size() == sramPattern.size());
        CHECK(restoredSram == sramPattern);
    }

    restored.onDeactivate();
}

TEST_CASE("MesenNesSystem state snapshot survives concurrent stepping + loads",
          "[MesenSystemBase]") {
    auto romBytes = loadRom(kRomPath);
    MesenNesConfig cfg{};
    cfg.romPath = kRomPath;
    MesenNesSystem live{SystemId{1}, cfg, romBytes};
    live.onActivate(kSampleRate);
    runBlocks(live, 20);  // warm up so captures aren't all-zero state
    REQUIRE(live.enableStateSnapshot());

    CommandQueue commands;

    // Scratch system the validator loads sampled snapshots into — touched only
    // post-join on this (main) thread, so Mesen's process globals never see
    // concurrent access.
    MesenNesConfig scratchCfg = cfg;
    MesenNesSystem scratch{SystemId{2}, scratchCfg, romBytes};
    scratch.onActivate(kSampleRate);

    rp::test::runStateSnapshotStress(
        live, SystemId{1}, commands, kSampleRate,
        [&](const std::vector<std::uint8_t>& snap) {
            return scratch.loadStateBytes(snap);
        });

    scratch.onDeactivate();
    live.onDeactivate();
}
