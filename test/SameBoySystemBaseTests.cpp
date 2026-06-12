// Tests for the SystemBase virtuals as implemented by SameBoySystem — the
// SRAM / savestate / clone / fastBoot / reload-on-rom-change surface that
// existed pre-parity as concrete methods on SameBoySystem and now route
// through SystemBase virtuals. Companion to MesenSystemBaseTests.cpp.
//
// The Game Boy ROM under test (LSDJ 9.4.2) ships in the sibling
// `../resources/roms/` directory outside the repo (see AGENTS.md). The
// CMake build wires its path through RETROPLUG_TEST_GB_ROM; tests skip
// gracefully when the file isn't present so a contributor without the
// resources tree can still run the rest of the suite.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "system/MemoryType.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/CommandQueue.hpp"

#include "StateSnapshotStress.hpp"

#ifndef RETROPLUG_TEST_GB_ROM
#  error "RETROPLUG_TEST_GB_ROM must be defined by CMake"
#endif

namespace {

constexpr double kSampleRate = 44100.0;
const std::string kRomPath = RETROPLUG_TEST_GB_ROM;

bool romAvailable() {
    std::error_code ec;
    return std::filesystem::exists(kRomPath, ec);
}

std::vector<std::uint8_t> loadRom() {
    std::ifstream f(kRomPath, std::ios::binary);
    REQUIRE(f.good());
    f.seekg(0, std::ios::end);
    const auto len = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(len));
    f.read(reinterpret_cast<char*>(bytes.data()), len);
    return bytes;
}

void runBlocks(SameBoySystem& sys, int blocks = 4, std::uint32_t blockSize = 256) {
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

// Snapshot Game Boy work RAM (8 KB). Used for "same state" equality where
// savestate bytes aren't necessarily byte-stable across round-trips.
std::vector<std::uint8_t> snapshotRam(SameBoySystem& sys) {
    auto acc = sys.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    if (!acc.valid() || acc.size() == 0) return {};
    return std::vector<std::uint8_t>(acc.data(), acc.data() + acc.size());
}

} // namespace

TEST_CASE("SameBoySystem::fastBoot reflects SameBoyConfig::fastBoot", "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath  = kRomPath;
    cfg.fastBoot = true;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    REQUIRE(sys.fastBoot().has_value());
    CHECK(*sys.fastBoot() == true);
    sys.setFastBoot(false);
    REQUIRE(sys.fastBoot().has_value());
    CHECK(*sys.fastBoot() == false);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::wantsRomReload toggles via setRomReload", "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    CHECK_FALSE(sys.wantsRomReload());
    sys.setRomReload(true);
    CHECK(sys.wantsRomReload());
    sys.setRomReload(false);
    CHECK_FALSE(sys.wantsRomReload());

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::romPath returns the configured path", "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    CHECK(sys.romPath() == kRomPath);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem SRAM round-trip via getMemory + saveSramBytes",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    auto accessor = sys.getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    REQUIRE(accessor.valid());
    REQUIRE(accessor.size() > 0);  // LSDJ carts always carry battery RAM

    std::vector<std::uint8_t> pattern(accessor.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<std::uint8_t>((i * 11 + 41) & 0xFF);
    }
    std::memcpy(accessor.data(), pattern.data(), pattern.size());

    auto snapshot = sys.saveSramBytes();
    REQUIRE(snapshot.size() == pattern.size());
    CHECK(snapshot == pattern);

    sys.clearSram();
    auto zeroed = sys.saveSramBytes();
    REQUIRE(zeroed.size() == pattern.size());
    for (auto b : zeroed) CHECK(b == 0);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem savestate round-trip restores work RAM",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    runBlocks(sys, 2);
    auto stateA = sys.saveStateBytes();
    auto ramAtA = snapshotRam(sys);
    REQUIRE(!stateA.empty());
    REQUIRE(!ramAtA.empty());

    runBlocks(sys, 50);
    auto stateB = sys.saveStateBytes();
    auto ramAtB = snapshotRam(sys);
    REQUIRE(ramAtB != ramAtA);
    CHECK(stateA != stateB);

    REQUIRE(sys.loadStateBytes(stateA));
    CHECK(snapshotRam(sys) == ramAtA);

    // Forward run from restored state is deterministic.
    runBlocks(sys, 50);
    CHECK(snapshotRam(sys) == ramAtB);

    // Bad inputs are rejected without crashing.
    std::vector<std::uint8_t> garbage(64, 0xCC);
    CHECK_FALSE(sys.loadStateBytes(garbage));
    CHECK_FALSE(sys.loadStateBytes({}));

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::clone produces an independent instance at matching state",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath     = kRomPath;
    cfg.linkGroupId = 7;  // exercises the "clone clears linkGroupId" contract
    SameBoySystem src{SystemId{1}, cfg, romBytes};
    src.onActivate(kSampleRate);

    // Step well into the boot ROM (a few hundred blocks @ 5.8 ms each =
    // ~290 ms). Shorter warm-ups leave the CPU in a tight idle loop where
    // work RAM is steady-state between samples, so the "stepping changes
    // state" check below would flake.
    runBlocks(src, 50);

    auto cloned = src.clone(SystemId{42}, kSampleRate);
    REQUIRE(cloned);

    CHECK(cloned.get() != static_cast<SystemBase*>(&src));
    CHECK(cloned->id() == SystemId{42});

    auto* clonedSys = dynamic_cast<SameBoySystem*>(cloned.get());
    REQUIRE(clonedSys);

    // Documented in the plan: the clone resets linkGroupId to 0 so it
    // doesn't silently inherit the source's link membership.
    CHECK(clonedSys->config_.linkGroupId == 0);

    // Same emulator state — same work-RAM bytes.
    auto srcRam   = snapshotRam(src);
    auto cloneRam = snapshotRam(*clonedSys);
    REQUIRE(!srcRam.empty());
    CHECK(srcRam == cloneRam);

    // Independent emulators: stepping one doesn't move the other.
    runBlocks(src, 50);
    CHECK(snapshotRam(*clonedSys) == cloneRam);
    CHECK(snapshotRam(src) != srcRam);

    // Determinism: stepping the clone by the same amount catches up.
    runBlocks(*clonedSys, 50);
    CHECK(snapshotRam(*clonedSys) == snapshotRam(src));

    src.onDeactivate();
}

TEST_CASE("SameBoySystem::snapshotConfig captures live SRAM and savestate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);
    runBlocks(sys, 5);
    auto ramAtSnap = snapshotRam(sys);

    auto snap = sys.snapshotConfig();
    const SameBoyConfig* sb = rfl::get_if<SameBoyConfig>(&snap.variant());
    REQUIRE(sb != nullptr);

    REQUIRE_FALSE(sb->savestate.empty());
    REQUIRE_FALSE(sb->sram.empty());

    // Hydrating a fresh system from the snapshot puts it at the same state.
    SameBoyConfig roundtripCfg = *sb;
    SameBoySystem roundtrip{SystemId{99}, roundtripCfg, romBytes};
    roundtrip.onActivate(kSampleRate);
    CHECK(snapshotRam(roundtrip) == ramAtSnap);
    roundtrip.onDeactivate();

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::restartEmulator preserves SRAM and drops savestate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    // Seed a savestate field on the config so we can prove restart wipes it.
    sys.config_.savestate = {1, 2, 3, 4};

    // Stamp a known pattern into SRAM. After restart it should still be there
    // (battery RAM survives a power cycle on real hardware).
    auto sramAcc = sys.getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    REQUIRE(sramAcc.valid());
    REQUIRE(sramAcc.size() > 0);
    std::vector<std::uint8_t> pattern(sramAcc.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<std::uint8_t>((i * 3 + 7) & 0xFF);
    }
    std::memcpy(sramAcc.data(), pattern.data(), pattern.size());

    sys.restartEmulator();

    // Live SRAM after restart matches the pattern we stamped pre-restart.
    auto sramAfter = sys.saveSramBytes();
    REQUIRE(sramAfter.size() == pattern.size());
    CHECK(sramAfter == pattern);

    // Savestate was cleared as part of the restart contract.
    CHECK(sys.config_.savestate.empty());

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem state snapshot round-trips the savestate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);
    runBlocks(sys, 50);

    // Enable arms an immediate first publish, so a single block after enable
    // is enough to land a snapshot (no waiting a full interval).
    REQUIRE(sys.enableStateSnapshot());
    runBlocks(sys, 1);

    std::vector<std::uint8_t> snap;
    REQUIRE(sys.readStateSnapshot(snap));
    REQUIRE(!snap.empty());

    // The snapshot is the full savestate: loading it into a fresh system
    // reproduces the source's work RAM at capture time (no stepping between
    // the publish and this read).
    const auto ramAtSnap = snapshotRam(sys);
    SameBoyConfig restoreCfg = cfg;
    SameBoySystem restored{SystemId{2}, restoreCfg, romBytes};
    restored.onActivate(kSampleRate);
    REQUIRE(restored.loadStateBytes(snap));
    CHECK(snapshotRam(restored) == ramAtSnap);
    restored.onDeactivate();

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem state snapshot region offsets locate SRAM in the savestate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);
    runBlocks(sys, 20);

    REQUIRE(sys.enableStateSnapshot());
    runBlocks(sys, 1);

    std::vector<std::uint8_t> snap;
    REQUIRE(sys.readStateSnapshot(snap));

    // SRAM sliced out of the savestate via the region table must equal a
    // direct battery read taken at the same instant (the v1 "regions live
    // inside the savestate" trick). Compared with no stepping in between.
    const auto sram = sys.saveSramBytes();
    REQUIRE(!sram.empty());
    const auto& region =
        sys.stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
    REQUIRE(region.size == sram.size());
    REQUIRE(static_cast<std::size_t>(region.offset) + region.size <= snap.size());
    const std::vector<std::uint8_t> sliced(
        snap.begin() + region.offset,
        snap.begin() + region.offset + region.size);
    CHECK(sliced == sram);

    // Same trick for the other GB regions carried in the savestate: WRAM and
    // VRAM sliced via stateRegions() must equal the live region read.
    auto sliceEqualsLive = [&](rp::MemoryType type) {
        const auto& r = sys.stateRegions()[static_cast<std::size_t>(type)];
        REQUIRE(r.size > 0);
        REQUIRE(static_cast<std::size_t>(r.offset) + r.size <= snap.size());
        auto acc = sys.getMemory(type, rp::AccessType::Read);
        REQUIRE(acc.valid());
        REQUIRE(acc.size() == r.size);
        const std::vector<std::uint8_t> regionSlice(
            snap.begin() + r.offset, snap.begin() + r.offset + r.size);
        const std::vector<std::uint8_t> live(acc.data(), acc.data() + acc.size());
        CHECK(regionSlice == live);
    };
    sliceEqualsLive(rp::MemoryType::Ram);
    sliceEqualsLive(rp::MemoryType::Vram);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::loadSramBytes overwrites live battery RAM",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    auto accessor = sys.getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    REQUIRE(accessor.valid());
    REQUIRE(accessor.size() > 0);  // LSDJ carts always carry battery RAM
    const std::size_t sramSize = accessor.size();

    std::vector<std::uint8_t> pattern(sramSize);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    }

    REQUIRE(sys.loadSramBytes(pattern));

    // The live battery RAM now reads back the loaded bytes, both via the
    // save path and the direct region view.
    CHECK(sys.saveSramBytes() == pattern);
    auto after = sys.getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    REQUIRE(after.valid());
    REQUIRE(after.size() == sramSize);
    CHECK(std::vector<std::uint8_t>(after.data(), after.data() + after.size()) == pattern);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem::cloneFromState seeds a clone from a captured savestate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath     = kRomPath;
    cfg.linkGroupId = 7;  // exercises the "clone clears linkGroupId" contract
    SameBoySystem src{SystemId{1}, cfg, romBytes};
    src.onActivate(kSampleRate);
    runBlocks(src, 50);  // step out of the idle loop so state is non-trivial

    // Capture the savestate the way the snapshot path hands it to Duplicate,
    // then build the clone from it (no live read of the source).
    const auto savestate = src.saveStateBytes();
    REQUIRE(!savestate.empty());
    auto cloned = src.cloneFromState(SystemId{42}, kSampleRate, savestate);
    REQUIRE(cloned);
    CHECK(cloned->id() == SystemId{42});

    auto* clonedSys = dynamic_cast<SameBoySystem*>(cloned.get());
    REQUIRE(clonedSys);
    CHECK(clonedSys->config_.linkGroupId == 0);

    // Same emulator state; SRAM sliced out of the savestate round-trips.
    auto srcRam = snapshotRam(src);
    REQUIRE(!srcRam.empty());
    CHECK(snapshotRam(*clonedSys) == srcRam);
    CHECK(clonedSys->saveSramBytes() == src.saveSramBytes());

    // Independent + deterministic, like clone().
    auto cloneRam = snapshotRam(*clonedSys);
    runBlocks(src, 50);
    CHECK(snapshotRam(*clonedSys) == cloneRam);   // stepping src doesn't move the clone
    CHECK(snapshotRam(src) != srcRam);
    runBlocks(*clonedSys, 50);
    CHECK(snapshotRam(*clonedSys) == snapshotRam(src));

    // Empty savestate is rejected.
    CHECK(src.cloneFromState(SystemId{43}, kSampleRate, {}) == nullptr);

    src.onDeactivate();
}

TEST_CASE("SameBoySystem honours config_.savestate at onActivate",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();

    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem original{SystemId{1}, cfg, romBytes};
    original.onActivate(kSampleRate);
    runBlocks(original, 50);

    auto stateBytes = original.saveStateBytes();
    auto ramAtSave  = snapshotRam(original);
    REQUIRE(!stateBytes.empty());
    REQUIRE(!ramAtSave.empty());
    original.onDeactivate();

    SameBoyConfig restoreCfg = cfg;
    restoreCfg.savestate = std::move(stateBytes);
    SameBoySystem restored{SystemId{2}, restoreCfg, romBytes};
    restored.onActivate(kSampleRate);

    CHECK(snapshotRam(restored) == ramAtSave);

    restored.onDeactivate();
}

TEST_CASE("SameBoySystem honours config_.sram at onActivate (without savestate)",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();

    // Build a battery RAM image with a known pattern. Sized to LSDJ's
    // 32 KB MBC5 battery slot — getMemory tells us the exact size first.
    SameBoyConfig probe;
    probe.romPath = kRomPath;
    SameBoySystem probeSys{SystemId{0}, probe, romBytes};
    probeSys.onActivate(kSampleRate);
    auto probeAcc = probeSys.getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    REQUIRE(probeAcc.valid());
    REQUIRE(probeAcc.size() > 0);
    const std::size_t sramSize = probeAcc.size();
    probeSys.onDeactivate();

    std::vector<std::uint8_t> sramPattern(sramSize);
    for (std::size_t i = 0; i < sramPattern.size(); ++i) {
        sramPattern[i] = static_cast<std::uint8_t>((i ^ 0xA5) & 0xFF);
    }

    // Boot a fresh system seeded ONLY with the SRAM pattern (no savestate
    // — per SameBoySystem.cpp:175 a savestate's embedded SRAM wins when
    // both are set, which would mask the test).
    SameBoyConfig cfg = probe;
    cfg.sram = sramPattern;
    SameBoySystem sys{SystemId{1}, cfg, romBytes};
    sys.onActivate(kSampleRate);

    auto restoredSram = sys.saveSramBytes();
    REQUIRE(restoredSram.size() == sramPattern.size());
    CHECK(restoredSram == sramPattern);

    sys.onDeactivate();
}

TEST_CASE("SameBoySystem state snapshot survives concurrent stepping + loads",
          "[SameBoySystemBase]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    auto romBytes = loadRom();
    SameBoyConfig cfg{};
    cfg.romPath = kRomPath;
    SameBoySystem live{SystemId{1}, cfg, romBytes};
    live.onActivate(kSampleRate);
    runBlocks(live, 20);  // warm up so captures aren't all-zero state
    REQUIRE(live.enableStateSnapshot());

    CommandQueue commands;

    // Scratch system the validator loads sampled snapshots into — touched only
    // post-join on this (main) thread.
    SameBoyConfig scratchCfg = cfg;
    SameBoySystem scratch{SystemId{2}, scratchCfg, romBytes};
    scratch.onActivate(kSampleRate);

    rp::test::runStateSnapshotStress(
        live, SystemId{1}, commands, kSampleRate,
        [&](const std::vector<std::uint8_t>& snap) {
            return scratch.loadStateBytes(snap);
        });

    scratch.onDeactivate();
    live.onDeactivate();
}
