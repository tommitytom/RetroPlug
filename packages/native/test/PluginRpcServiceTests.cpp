// PluginRpcService save/load/duplicate/getMemory glue — specifically the
// snapshot-preferred-vs-direct-read fallback and the command-queue load path.
//
// The save/load helpers are private; they're driven the way the plugin drives
// them, via the public open*Browser() + onFileBrowserSelected() flow. The
// service runs over a real Project holding a real SameBoySystem; reads/writes
// go to /tmp. Requires a Game Boy ROM (RETROPLUG_TEST_GB_ROM); skips if absent.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>   // _getpid
#else
#include <unistd.h>    // getpid
#endif

#include "PluginRpcService.hpp"
#include "config/RecentFiles.hpp"
#include "config/UserConfig.hpp"
#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/MemoryType.hpp"
#include "system/SramAutoSave.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"

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

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<std::uint8_t> toBytes(const rfl::Bytestring& b) {
    std::vector<std::uint8_t> out(b.size());
    for (std::size_t i = 0; i < b.size(); ++i) out[i] = std::to_integer<std::uint8_t>(b[i]);
    return out;
}

std::vector<std::uint8_t> loadRom() {
    std::ifstream f(kRomPath, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

void runBlocks(SameBoySystem& sys, int blocks, std::uint32_t blockSize = 2048) {
    std::vector<float> l(blockSize), r(blockSize);
    float* outs[2] = { l.data(), r.data() };
    AudioBlockInfo info{};
    info.frames = blockSize;
    info.sampleRate = kSampleRate;
    info.tempo = 120.0;
    info.ppqPosBlockStart = 0.0;
    info.transportPlaying = false;
    for (int i = 0; i < blocks; ++i) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys.onProcess(info, outs);
    }
}

// Owns a Project + a single activated SameBoySystem + the service over them,
// plus an event-capture log. Mirrors how PluginJsBridge wires the service.
struct Fixture {
    std::vector<std::uint8_t>  rom = loadRom();
    Project                    project;
    CommandQueue               commands;
    EventQueue                 events;
    std::atomic<double>        sampleRate{kSampleRate};
    std::atomic<SystemId>      focused{0};
    SystemId                   id{};
    SameBoySystem*             sys = nullptr;
    std::vector<std::pair<std::string, std::string>> emitted;
    PluginRpcService           service{&project, &commands, &events, &sampleRate, &focused};

    Fixture() {
        id = project.nextSystemId();
        SameBoyConfig cfg{};
        cfg.romPath = kRomPath;
        auto owned = std::make_unique<SameBoySystem>(id, cfg, rom);
        sys = owned.get();
        sys->onActivate(kSampleRate);
        project.adoptSystem(owned.release());
        service.setEmitEventCallback(
            [this](const std::string& ch, const std::string& p) { emitted.emplace_back(ch, p); });
        // A no-op browser so the open*Browser() calls succeed (they bail when
        // no callback is registered).
        service.setOpenFileBrowserCallback([](const char*, bool, const char*, const char*, const char*) {});
    }

    bool sawEvent(const std::string& ch) const {
        return std::any_of(emitted.begin(), emitted.end(),
                           [&](const auto& e) { return e.first == ch; });
    }
};

} // namespace

TEST_CASE("PluginRpcService saveState falls back to a live read without a snapshot",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    runBlocks(*fx.sys, 4);

    const auto expected = fx.sys->saveStateBytes();   // live read at this instant
    const std::string path = "/tmp/rpc_state_fallback.ss0";
    REQUIRE(fx.service.openSaveStateBrowser(fx.id));
    fx.service.onFileBrowserSelected(path.c_str());

    CHECK(fx.sawEvent("state-saved"));
    CHECK(readFile(path) == expected);
}

TEST_CASE("PluginRpcService saveState prefers the published snapshot",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    runBlocks(*fx.sys, 4);

    REQUIRE(fx.sys->enableStateSnapshot());
    runBlocks(*fx.sys, 1);                 // arms + publishes the first snapshot
    std::vector<std::uint8_t> snap;
    REQUIRE(fx.sys->readStateSnapshot(snap));

    // Advance the live emulator WITHOUT crossing the ~0.5s publish interval, so
    // the snapshot now differs from the live state.
    runBlocks(*fx.sys, 8);
    REQUIRE(fx.sys->saveStateBytes() != snap);

    const std::string path = "/tmp/rpc_state_snapshot.ss0";
    REQUIRE(fx.service.openSaveStateBrowser(fx.id));
    fx.service.onFileBrowserSelected(path.c_str());

    // The file is the snapshot, not the (newer) live state — proves the
    // snapshot-preferred branch ran.
    CHECK(readFile(path) == snap);
    CHECK(readFile(path) != fx.sys->saveStateBytes());
}

TEST_CASE("PluginRpcService saveSram writes the battery RAM",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    const std::string path = "/tmp/rpc_sram.sav";
    REQUIRE(fx.service.openSaveSramBrowser(fx.id));
    fx.service.onFileBrowserSelected(path.c_str());

    CHECK(fx.sawEvent("sram-saved"));
    CHECK(readFile(path) == fx.sys->saveSramBytes());
}

TEST_CASE("PluginRpcService getMemory prefers the snapshot slice",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    runBlocks(*fx.sys, 4);

    REQUIRE(fx.sys->enableStateSnapshot());
    runBlocks(*fx.sys, 1);                 // publish
    std::vector<std::uint8_t> snap;
    REQUIRE(fx.sys->readStateSnapshot(snap));
    const auto& ramRegion = fx.sys->stateRegions()[static_cast<std::size_t>(rp::MemoryType::Ram)];
    REQUIRE(ramRegion.size > 0);
    const std::vector<std::uint8_t> snapRam(
        snap.begin() + ramRegion.offset, snap.begin() + ramRegion.offset + ramRegion.size);

    // Advance the live emulator so its RAM diverges from the snapshot.
    runBlocks(*fx.sys, 8);
    auto liveAcc = fx.sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    const std::vector<std::uint8_t> liveRam(liveAcc.data(), liveAcc.data() + liveAcc.size());
    REQUIRE(liveRam != snapRam);

    auto resp = fx.service.getMemory(fx.id, static_cast<std::uint32_t>(rp::MemoryType::Ram), 0, 0);
    REQUIRE(resp.has_value());
    const std::vector<std::uint8_t> got = toBytes(resp->bytes);
    CHECK(got == snapRam);     // came from the snapshot, not the live region
    CHECK(got != liveRam);
}

TEST_CASE("PluginRpcService load* slurp a file and queue a command",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;

    SECTION("loadState pushes a LoadState command with the file bytes") {
        const std::string path = "/tmp/rpc_loadstate.ss0";
        const std::vector<std::uint8_t> payload{1, 2, 3, 4, 5};
        { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(payload.data()), payload.size()); }

        REQUIRE(fx.service.openLoadStateBrowser(fx.id));
        fx.service.onFileBrowserSelected(path.c_str());
        CHECK(fx.sawEvent("state-loaded"));

        Command cmd;
        REQUIRE(fx.commands.tryPop(cmd));
        CHECK(cmd.kind == Command::Kind::LoadState);
        std::unique_ptr<std::vector<std::uint8_t>> bytes(cmd.payload.loadState.bytes);
        REQUIRE(bytes);
        CHECK(*bytes == payload);
    }

    SECTION("loadSram pushes a LoadSram command with the file bytes") {
        const std::string path = "/tmp/rpc_loadsram.sav";
        const std::vector<std::uint8_t> payload{9, 8, 7, 6};
        { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(payload.data()), payload.size()); }

        REQUIRE(fx.service.openLoadSramBrowser(fx.id));
        fx.service.onFileBrowserSelected(path.c_str());
        CHECK(fx.sawEvent("sram-loaded"));

        Command cmd;
        REQUIRE(fx.commands.tryPop(cmd));
        CHECK(cmd.kind == Command::Kind::LoadSram);
        std::unique_ptr<std::vector<std::uint8_t>> bytes(cmd.payload.loadSram.bytes);
        REQUIRE(bytes);
        CHECK(*bytes == payload);
    }
}

TEST_CASE("PluginRpcService duplicateSystem queues an AddSystem with a clone",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    runBlocks(*fx.sys, 20);

    REQUIRE(fx.service.duplicateSystem(fx.id));

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    CHECK(cmd.kind == Command::Kind::AddSystem);
    // Ownership of the cloned system transfers via the command; adopt + free it.
    SystemBase* clone = cmd.payload.addSystem.newSystem;
    REQUIRE(clone != nullptr);
    CHECK(clone != static_cast<SystemBase*>(fx.sys));
    // Same emulator state as the source (clone path, snapshot or live).
    auto srcAcc   = fx.sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    auto cloneAcc = clone->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(srcAcc.valid());
    REQUIRE(cloneAcc.valid());
    CHECK(std::vector<std::uint8_t>(cloneAcc.data(), cloneAcc.data() + cloneAcc.size()) ==
          std::vector<std::uint8_t>(srcAcc.data(), srcAcc.data() + srcAcc.size()));
    delete clone;
}

// ---------------------------------------------------------------------------
// SRAM auto-save (system/SramAutoSave.hpp + PluginRpcService::pumpSramAutoSave).
// All write-tests use a system whose romPath is under /tmp so the sibling .sav
// lands there, never next to the real ROM in ../resources.
// ---------------------------------------------------------------------------
namespace {

std::atomic<int> g_tmpCounter{0};

// Catch2's ctest discovery runs every TEST_CASE in its own process, where the
// process-local g_tmpCounter restarts at 0 — so parallel `ctest -j` runs would
// otherwise collide on identical /tmp names (one fixture's remove_all wiping
// another's live dir). Qualify every temp path with the OS process id.
int processToken() {
    static const int pid =
#if defined(_WIN32)
        ::_getpid();
#else
        static_cast<int>(::getpid());
#endif
    return pid;
}

std::string uniqueTmpPath(const char* tag, const char* ext) {
    const int n = g_tmpCounter.fetch_add(1);
    auto p = std::filesystem::temp_directory_path() /
             (std::string("rp_as_") + tag + "_" + std::to_string(processToken()) +
              "_" + std::to_string(n) + ext);
    return p.string();
}

bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

// An activated SameBoySystem at a writable /tmp romPath, adopted into `project`.
struct TmpSys {
    SystemId       id{};
    SameBoySystem* sys = nullptr;
    std::string    romPath;
    std::string    savPath;
};

TmpSys makeTmpSys(Project& project, const std::vector<std::uint8_t>& rom,
                  const char* tag, bool writeRomFile = false) {
    TmpSys t;
    t.romPath = uniqueTmpPath(tag, ".gb");
    t.savPath = rp::sram_autosave::siblingSavPath(t.romPath);
    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);            // start from a clean slate
    if (writeRomFile) {
        std::ofstream f(t.romPath, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(rom.data()),
                static_cast<std::streamsize>(rom.size()));
    }
    SameBoyConfig cfg{};
    cfg.romPath = t.romPath;
    t.id = project.nextSystemId();
    auto owned = std::make_unique<SameBoySystem>(t.id, cfg, rom);
    t.sys = owned.get();
    t.sys->onActivate(kSampleRate);
    project.adoptSystem(owned.release());
    return t;
}

// Project + queues + a UserConfig(tempdir) wired into the service, for the pump
// tests (the pump is gated on userConfig_->autoSaveSram()).
struct PumpFixture {
    std::filesystem::path cfgDir = [] {
        auto d = std::filesystem::temp_directory_path() /
                 ("rp_as_cfg_" + std::to_string(processToken()) + "_" +
                  std::to_string(g_tmpCounter.fetch_add(1)));
        std::filesystem::create_directories(d);
        return d;
    }();
    UserConfig             userConfig{cfgDir};
    Project                project;
    CommandQueue           commands;
    EventQueue             events;
    std::atomic<double>    sampleRate{kSampleRate};
    std::atomic<SystemId>  focused{0};
    PluginRpcService       service{&project, &commands, &events, &sampleRate, &focused,
                                   &userConfig};

    PumpFixture() { userConfig.start(); }
    ~PumpFixture() { std::error_code ec; std::filesystem::remove_all(cfgDir, ec); }
};

} // namespace

// loadRomFromPath() reads the sibling `<rom>.sav` inline in buildSystemFromPath
// (distinct from the project-load path's slurpSiblingSav). This is the exact
// flow the file-open dialog drives. Regression guard for the Windows port where
// the SameBoy GB_gameboy_t C-vs-C++ layout divergence (see SameBoySystem.cpp's
// GB_alloc note) made battery-RAM handling fragile.
TEST_CASE("loadRomFromPath applies the sibling .sav",
          "[PluginRpcService][sram]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    const std::string romPath = uniqueTmpPath("repro", ".gb");
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);
    {
        std::ofstream o(romPath, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(rom.data()),
                static_cast<std::streamsize>(rom.size()));
    }

    // Probe the cart's battery size.
    std::size_t n = 0;
    {
        SameBoyConfig c{}; c.romPath = romPath;
        SameBoySystem s{SystemId{99}, c, rom};
        s.onActivate(kSampleRate);
        n = s.saveSramBytes().size();
        s.onDeactivate();
    }
    REQUIRE(n > 0);

    const std::vector<std::uint8_t> image(n, 0x3C);
    {
        std::ofstream o(savPath, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(image.data()),
                static_cast<std::streamsize>(image.size()));
    }

    PumpFixture f;
    REQUIRE(f.service.loadRomFromPath(romPath));   // == the file-dialog flow

    Command cmd;
    REQUIRE(f.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->saveSramBytes() == image);           // <-- the sibling .sav got applied
    delete cmd.payload.loadRom.newSystem;

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::path rplg(romPath); rplg.replace_extension(".rplg");
    std::filesystem::remove(rplg, ec);
}

TEST_CASE("SRAM auto-save round-trips through the sibling .sav on reload",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();
    Project project;
    auto t = makeTmpSys(project, rom, "rt", /*writeRomFile*/ true);

    const std::size_t n = t.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> image(n, 0x5A);
    REQUIRE(t.sys->loadSramBytes(image));

    std::optional<std::uint64_t> hash;
    REQUIRE(rp::autoSaveSramToSibling(*t.sys, hash));   // writes the /tmp sibling
    REQUIRE(fileExists(t.savPath));

    // Rebuild from the path (no embedded bytes): addSystem re-reads the ROM AND
    // slurps the sibling .sav, exactly as a path-only project load does.
    SameBoyConfig cfg{};
    cfg.romPath = t.romPath;
    const SystemId rid = project.addSystem(cfg);
    REQUIRE(rid != 0);
    SystemBase* restored = project.findSystem(rid);
    REQUIRE(restored != nullptr);
    restored->onActivate(kSampleRate);
    CHECK(restored->saveSramBytes() == image);          // auto-saved SRAM came back

    std::error_code ec;
    std::filesystem::remove(t.romPath, ec);
    std::filesystem::remove(t.savPath, ec);
}

TEST_CASE("SRAM auto-save reads battery RAM from the published snapshot",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();
    Project project;
    auto t = makeTmpSys(project, rom, "snap");

    runBlocks(*t.sys, 4);
    REQUIRE(t.sys->enableStateSnapshot());
    runBlocks(*t.sys, 1);                                // arms + publishes
    std::vector<std::uint8_t> snap;
    REQUIRE(t.sys->readStateSnapshot(snap));
    const auto& reg = t.sys->stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
    REQUIRE(reg.size > 0);
    const std::vector<std::uint8_t> snapSram(
        snap.begin() + reg.offset, snap.begin() + reg.offset + reg.size);

    // Diverge the LIVE battery from the snapshot (no further blocks => no
    // republish), then auto-save must write the SNAPSHOT bytes, not the live ones.
    const std::vector<std::uint8_t> live(reg.size, 0x3C);
    REQUIRE(t.sys->loadSramBytes(live));
    REQUIRE(t.sys->saveSramBytes() != snapSram);

    std::optional<std::uint64_t> hash;
    REQUIRE(rp::autoSaveSramToSibling(*t.sys, hash));
    const auto written = readFile(t.savPath);
    CHECK(written == snapSram);                          // snapshot branch ran
    CHECK(written != t.sys->saveSramBytes());            // not the live read

    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);
}

TEST_CASE("SRAM auto-save writes on change, skips when unchanged, never rewrites a match",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();
    Project project;
    auto t = makeTmpSys(project, rom, "contract");

    const std::size_t n = t.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    REQUIRE(t.sys->loadSramBytes(std::vector<std::uint8_t>(n, 0x11)));

    std::optional<std::uint64_t> hash;
    REQUIRE_FALSE(fileExists(t.savPath));
    CHECK(rp::autoSaveSramToSibling(*t.sys, hash));      // absent -> create
    REQUIRE(fileExists(t.savPath));
    CHECK(readFile(t.savPath) == t.sys->saveSramBytes());

    CHECK_FALSE(rp::autoSaveSramToSibling(*t.sys, hash)); // unchanged -> skip

    REQUIRE(t.sys->loadSramBytes(std::vector<std::uint8_t>(n, 0x22)));
    CHECK(rp::autoSaveSramToSibling(*t.sys, hash));       // changed -> write
    CHECK(readFile(t.savPath) == t.sys->saveSramBytes());

    // First observation with a sibling already equal to the battery: seed the
    // hash, do NOT rewrite (the load-time no-thrash branch).
    std::optional<std::uint64_t> fresh;
    CHECK_FALSE(rp::autoSaveSramToSibling(*t.sys, fresh));
    CHECK(fresh.has_value());
    CHECK_FALSE(rp::autoSaveSramToSibling(*t.sys, fresh)); // still a no-op

    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);
}

TEST_CASE("pumpSramAutoSave honours the preference gate and the throttle",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    PumpFixture fx;
    const auto rom = loadRom();
    auto t = makeTmpSys(fx.project, rom, "pump");

    const std::size_t n = t.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> imageA(n, 0x41);
    REQUIRE(t.sys->loadSramBytes(imageA));

    // Preference OFF -> no write at all.
    REQUIRE_FALSE(fx.userConfig.autoSaveSram());
    fx.service.pumpSramAutoSave();
    CHECK_FALSE(fileExists(t.savPath));

    // ON -> the first pump flushes (the throttle starts disarmed).
    REQUIRE(fx.userConfig.setAutoSaveSram(true));
    fx.service.pumpSramAutoSave();
    REQUIRE(fileExists(t.savPath));
    CHECK(readFile(t.savPath) == imageA);

    // Within the interval a fresh change is NOT flushed (default 5s throttle;
    // the two pumps are microseconds apart).
    const std::vector<std::uint8_t> imageB(n, 0x42);
    REQUIRE(t.sys->loadSramBytes(imageB));
    fx.service.pumpSramAutoSave();
    CHECK(readFile(t.savPath) == imageA);                // still the throttled-out value

    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);
}

TEST_CASE("pumpSramAutoSave writes each system's own sibling and prunes removed ones",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    PumpFixture fx;
    fx.service.setSramAutoSaveIntervalSec(0.0);          // disable throttle for the test
    REQUIRE(fx.userConfig.setAutoSaveSram(true));
    const auto rom = loadRom();

    auto a = makeTmpSys(fx.project, rom, "multiA");
    auto b = makeTmpSys(fx.project, rom, "multiB");
    const std::size_t n = a.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> imgA(n, 0xA0), imgB(n, 0xB0);
    REQUIRE(a.sys->loadSramBytes(imgA));
    REQUIRE(b.sys->loadSramBytes(imgB));

    fx.service.pumpSramAutoSave();                       // one pump flushes both

    REQUIRE(fileExists(a.savPath));
    REQUIRE(fileExists(b.savPath));
    CHECK(a.savPath != b.savPath);
    CHECK(readFile(a.savPath) == imgA);                  // independent per-system
    CHECK(readFile(b.savPath) == imgB);

    // Remove one system; the next pump must prune its hash entry and not crash,
    // and must not touch its sibling again.
    SystemBase* released = fx.project.removeSystemAndRelease(a.id);
    REQUIRE(released != nullptr);
    delete released;
    std::error_code ec;
    std::filesystem::remove(a.savPath, ec);              // delete; a pruned system won't rewrite it
    fx.service.pumpSramAutoSave();
    CHECK_FALSE(fileExists(a.savPath));                  // removed system not re-saved
    CHECK(fileExists(b.savPath));                        // survivor still managed

    std::filesystem::remove(a.savPath, ec);
    std::filesystem::remove(b.savPath, ec);
}

TEST_CASE("SRAM auto-save is a no-op without a romPath or a battery",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    SECTION("empty romPath (embed-only system) writes nothing") {
        Project project;
        SameBoyConfig cfg{};                             // romPath deliberately empty
        const SystemId id = project.nextSystemId();
        auto owned = std::make_unique<SameBoySystem>(id, cfg, rom);
        SameBoySystem* sys = owned.get();
        sys->onActivate(kSampleRate);
        project.adoptSystem(owned.release());

        std::optional<std::uint64_t> hash;
        CHECK_FALSE(rp::autoSaveSramToSibling(*sys, hash));
    }

    SECTION("batteryless cart creates no sibling") {
#ifdef RETROPLUG_TEST_MGB_ROM
        const std::string mgbPath = RETROPLUG_TEST_MGB_ROM;
        if (!fileExists(mgbPath)) SKIP("mGB ROM missing at " << mgbPath);
        std::ifstream f(mgbPath, std::ios::binary);
        const std::vector<std::uint8_t> mgb(std::istreambuf_iterator<char>(f), {});
        REQUIRE(!mgb.empty());

        Project project;
        const std::string romPath = uniqueTmpPath("mgb", ".gb");
        const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);
        std::error_code ec;
        std::filesystem::remove(savPath, ec);
        SameBoyConfig cfg{};
        cfg.romPath = romPath;
        const SystemId id = project.nextSystemId();
        auto owned = std::make_unique<SameBoySystem>(id, cfg, mgb);
        SameBoySystem* sys = owned.get();
        sys->onActivate(kSampleRate);
        project.adoptSystem(owned.release());

        if (!sys->saveSramBytes().empty()) SKIP("this mGB build reports a battery");
        std::optional<std::uint64_t> hash;
        CHECK_FALSE(rp::autoSaveSramToSibling(*sys, hash));
        CHECK_FALSE(fileExists(savPath));
#else
        SKIP("RETROPLUG_TEST_MGB_ROM not defined");
#endif
    }
}

// ---------------------------------------------------------------------------
// Unsaved-changes tracking for the standalone close prompt.
// ---------------------------------------------------------------------------

TEST_CASE("unsaved: a project edit flips hasUnsavedChanges; saving clears it",
          "[PluginRpcService][unsaved]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    // Fresh project (system adopted directly, no mutating RPC) reads clean; the
    // first check also seeds the SRAM baseline so SRAM doesn't read dirty.
    CHECK_FALSE(fx.service.hasUnsavedChanges());

    REQUIRE(fx.service.setZoom(4));
    CHECK(fx.service.hasUnsavedChanges());
    CHECK(fx.service.getUnsavedSummary().project);

    // Save via the same open-browser + onFileBrowserSelected path the UI uses.
    const std::string proj = "/tmp/rpc_unsaved_proj.rplg";
    REQUIRE(fx.service.openSaveProjectBrowser());
    fx.service.onFileBrowserSelected(proj.c_str());
    CHECK(fx.sawEvent("project-saved"));
    CHECK_FALSE(fx.service.hasUnsavedChanges());

    std::error_code ec;
    std::filesystem::remove(proj, ec);
}

// ---------------------------------------------------------------------------
// Zoom: getProjectZoom returns the RAW per-project value (0 = inherit the user
// default), getZoom resolves it, and setZoom accepts the full 0..6 range so a
// project can be put back on the default (stored as 0).
// ---------------------------------------------------------------------------
TEST_CASE("zoom: getProjectZoom is raw, getZoom resolves, setZoom accepts 0..6",
          "[PluginRpcService][zoom]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;

    // Raw 0 = inherit; with no UserConfig getZoom falls back to the default 3.
    fx.project.config().settings.zoom = 0;
    CHECK(fx.service.getProjectZoom() == 0);
    CHECK(fx.service.getZoom() == 3);

    // Explicit value: raw == resolved.
    fx.project.config().settings.zoom = 5;
    CHECK(fx.service.getProjectZoom() == 5);
    CHECK(fx.service.getZoom() == 5);

    // setZoom spans 0..6 (0 = back to inherit); out-of-range is rejected.
    CHECK(fx.service.setZoom(0));
    CHECK(fx.service.setZoom(6));
    CHECK_FALSE(fx.service.setZoom(7));
}

TEST_CASE("unsaved: an SRAM change flips hasUnsavedChanges; saveDirtySram clears it",
          "[PluginRpcService][unsaved]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    PumpFixture fx;
    const auto rom = loadRom();
    auto t = makeTmpSys(fx.project, rom, "unsaved");

    fx.service.pumpSramAutoSave();                 // seeds the load baseline
    CHECK_FALSE(fx.service.hasUnsavedChanges());

    const std::size_t n = t.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    REQUIRE(t.sys->loadSramBytes(std::vector<std::uint8_t>(n, 0x5A)));
    CHECK(fx.service.hasUnsavedChanges());
    CHECK(fx.service.getUnsavedSummary().sramSystems == 1u);

    REQUIRE(fx.service.saveDirtySram());           // writes the /tmp sibling
    CHECK_FALSE(fx.service.hasUnsavedChanges());

    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);
}

TEST_CASE("unsaved: quitStandalone fires the quit callback",
          "[PluginRpcService][unsaved]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;
    bool quit = false;
    fx.service.setQuitCallback([&]{ quit = true; });
    CHECK(fx.service.quitStandalone());
    CHECK(quit);
}

// ---------------------------------------------------------------------------
// Recent-projects list. Loading a ROM writes a thin <rom>.rplg beside it and
// tracks the PROJECT (not the ROM). All paths live under /tmp so the sibling
// .rplg never lands next to the real ROM in ../resources.
// ---------------------------------------------------------------------------
namespace {

void writeBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

std::string siblingRplg(const std::string& romPath) {
    auto p = std::filesystem::path(romPath);
    p.replace_extension(".rplg");
    return p.string();
}

// A thin (path-only) project referencing `romPath`, as projectConfigToJsonFile
// produces. Used to pre-seed an existing sibling project on disk.
void writeThinProject(const std::string& path, const std::string& romPath) {
    SameBoyConfig sb{};
    sb.romPath = romPath;
    ProjectConfig cfg;
    cfg.systems.push_back(sb);
    const std::string json = projectConfigToJsonFile(cfg);
    writeBytes(path, std::vector<std::uint8_t>(json.begin(), json.end()));
}

// Project + queues + a RecentFiles(tempdir) wired into the service, mirroring
// how PluginJsBridge wires it. Drains any LoadRom command the service queues so
// the heap-owned system is freed (no leak under asan).
struct RecentFixture {
    std::vector<std::uint8_t>  rom = loadRom();
    std::filesystem::path      cfgDir = [] {
        auto d = std::filesystem::temp_directory_path() /
                 ("rp_recent_cfg_" + std::to_string(processToken()) + "_" +
                  std::to_string(g_tmpCounter.fetch_add(1)));
        std::filesystem::create_directories(d);
        return d;
    }();
    RecentFiles                recent{cfgDir};
    Project                    project;
    CommandQueue               commands;
    EventQueue                 events;
    std::atomic<double>        sampleRate{kSampleRate};
    std::atomic<SystemId>      focused{0};
    std::vector<std::pair<std::string, std::string>> emitted;
    PluginRpcService           service{&project, &commands, &events, &sampleRate,
                                       &focused, /*userConfig*/ nullptr, &recent};

    RecentFixture() {
        recent.start();
        service.setEmitEventCallback(
            [this](const std::string& ch, const std::string& p) { emitted.emplace_back(ch, p); });
        service.setOpenFileBrowserCallback([](const char*, bool, const char*, const char*, const char*) {});
    }
    ~RecentFixture() {
        drainSystems();
        std::error_code ec;
        std::filesystem::remove_all(cfgDir, ec);
    }

    // Free any heap payloads the service queued onto the command bus (the DSP
    // would normally own these; here there's no DSP draining the queue).
    void drainSystems() {
        Command cmd;
        while (commands.tryPop(cmd)) {
            if (cmd.kind == Command::Kind::LoadRom)     delete cmd.payload.loadRom.newSystem;
            if (cmd.kind == Command::Kind::AddSystem)   delete cmd.payload.addSystem.newSystem;
            if (cmd.kind == Command::Kind::LoadProject) delete cmd.payload.loadProject.config;
        }
    }

    bool sawEvent(const std::string& ch) const {
        return std::any_of(emitted.begin(), emitted.end(),
                           [&](const auto& e) { return e.first == ch; });
    }
};

} // namespace

TEST_CASE("recent: loading a ROM writes a sibling project and tracks it",
          "[PluginRpcService][recent]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("load", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string projPath = siblingRplg(romPath);
    std::error_code ec;
    std::filesystem::remove(projPath, ec);

    REQUIRE(fx.service.loadRomFromPath(romPath));

    // A thin project was written beside the ROM.
    REQUIRE(fileExists(projPath));
    const auto bytes = readFile(projPath);
    auto cfg = projectConfigFromBytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->systems.size() == 1);

    // Recent holds the PROJECT (not the ROM), present, and is the current path.
    auto list = fx.service.getRecentFiles();
    REQUIRE(list.size() == 1);
    CHECK(std::filesystem::path(list[0].path).extension() == ".rplg");
    CHECK_FALSE(list[0].missing);
    CHECK(fx.service.getCurrentProjectPath() == projPath);

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(projPath, ec);
}

TEST_CASE("recent: getRecentFiles flags a deleted project as missing",
          "[PluginRpcService][recent]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("missing", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string projPath = siblingRplg(romPath);

    REQUIRE(fx.service.loadRomFromPath(romPath));
    CHECK_FALSE(fx.service.getRecentFiles().at(0).missing);

    std::error_code ec;
    std::filesystem::remove(projPath, ec);
    CHECK(fx.service.getRecentFiles().at(0).missing);

    std::filesystem::remove(romPath, ec);
}

TEST_CASE("recent: loading a ROM with an existing sibling opens it, no overwrite",
          "[PluginRpcService][recent]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("existing", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string projPath = siblingRplg(romPath);
    writeThinProject(projPath, romPath);
    const auto before = readFile(projPath);

    REQUIRE(fx.service.loadRomFromPath(romPath));

    // Delegated to the project loader (no fresh rom-load) and left the file as-is.
    CHECK(fx.sawEvent("project-loaded"));
    CHECK_FALSE(fx.sawEvent("rom-loaded"));
    CHECK(readFile(projPath) == before);
    CHECK(fx.service.getRecentFiles().size() == 1);

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(projPath, ec);
}

TEST_CASE("recent: rename sets an alias and remove drops the entry",
          "[PluginRpcService][recent]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("rename", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string projPath = siblingRplg(romPath);
    REQUIRE(fx.service.loadRomFromPath(romPath));

    REQUIRE(fx.service.renameRecentFile(projPath, "My Song"));
    CHECK(fx.service.getRecentFiles().at(0).name == "My Song");

    REQUIRE(fx.service.removeRecentFile(projPath));
    CHECK(fx.service.getRecentFiles().empty());

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(projPath, ec);
}

TEST_CASE("recent: openRecentRelinkBrowser + selection relinks the entry",
          "[PluginRpcService][recent]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("relink", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string projPath = siblingRplg(romPath);
    REQUIRE(fx.service.loadRomFromPath(romPath));

    // Point the entry at a new project file (the file-browser stand-in).
    const std::string newProj = uniqueTmpPath("relinked", ".rplg");
    writeThinProject(newProj, romPath);

    REQUIRE(fx.service.openRecentRelinkBrowser(projPath));
    fx.service.onFileBrowserSelected(newProj.c_str());

    auto list = fx.service.getRecentFiles();
    REQUIRE(list.size() == 1);
    CHECK(std::filesystem::path(list[0].path).filename() ==
          std::filesystem::path(newProj).filename());
    CHECK_FALSE(list[0].missing);

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(projPath, ec);
    std::filesystem::remove(newProj, ec);
}

// ---------------------------------------------------------------------------
// Embedded mGB: loaded from the binary (no file), so it carries no romPath
// (→ no .sav / ROM-watcher), no battery SRAM, and an "mgb" marker so a saved
// project re-supplies the bytes on reload. loadMgb does not touch recent files.
// ---------------------------------------------------------------------------
TEST_CASE("loadMgb builds a pathless, marked embedded mGB system",
          "[PluginRpcService][mgb]") {
    PumpFixture f;
    REQUIRE(f.service.loadMgb());

    Command cmd;
    REQUIRE(f.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);

    CHECK(sb->romPath().empty());          // pathless → SRAM auto-save + watcher skip it
    CHECK(sb->saveSramBytes().empty());    // mGB has no battery SRAM → nothing to .sav
    CHECK(sb->config_.embeddedRom == "mgb"); // marker → reloadable from a saved project
    CHECK(sb->config_.embedRom == false);  // bytes live in the binary, not saved state

    delete cmd.payload.loadRom.newSystem;
}

TEST_CASE("an embedded-mGB project survives a thin round-trip",
          "[PluginRpcService][mgb]") {
    // A thin .rplg / DPF-state save strips romBytes; only the "mgb" marker and
    // empty romPath remain. The project loader must re-supply the bytes from
    // the binary so the system still loads.
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.embeddedRom = "mgb";
    sb.embedRom    = false;                // no romBytes; no romPath
    cfg.systems.push_back(sb);

    const std::string json = projectConfigToJsonFile(cfg);   // strips binaries
    const auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed);
    REQUIRE(parsed->systems.size() == 1);

    Project project;
    const SystemId id = project.loadFromConfig(*parsed);
    CHECK(id != 0);                        // loaded despite no bytes/path on disk
    CHECK(project.systems().size() == 1);
}

// ---------------------------------------------------------------------------
// New Project: discards the current project for an empty default one. Queues a
// LoadProject with a zero-system config and resets the load/save bookkeeping
// (remembered path + dirty flag) so a follow-up Save opens the dialog.
// ---------------------------------------------------------------------------
TEST_CASE("newProject queues an empty default project and clears save state",
          "[PluginRpcService][project-new]") {
    PumpFixture f;
    REQUIRE(f.service.newProject());

    Command cmd;
    REQUIRE(f.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadProject);
    ProjectConfig* cfg = cmd.payload.loadProject.config;
    REQUIRE(cfg != nullptr);
    CHECK(cfg->systems.empty());        // a clean slate
    CHECK(cfg->settings.zoom == 0);     // default settings (0 = inherit user default)
    delete cfg;

    CHECK(f.service.getCurrentProjectPath().empty());
    CHECK(f.service.hasUnsavedChanges() == false);
}

TEST_CASE("newProject forgets a loaded project's remembered path",
          "[PluginRpcService][project-new]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    const std::string romPath = uniqueTmpPath("newproj", ".gb");
    {
        std::ofstream o(romPath, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(rom.data()),
                static_cast<std::streamsize>(rom.size()));
    }

    PumpFixture f;
    REQUIRE(f.service.loadRomFromPath(romPath));   // sets currentProjectPath_ (sibling .rplg)
    CHECK_FALSE(f.service.getCurrentProjectPath().empty());
    { Command c; REQUIRE(f.commands.tryPop(c)); delete c.payload.loadRom.newSystem; }

    REQUIRE(f.service.newProject());
    CHECK(f.service.getCurrentProjectPath().empty());   // the path was dropped
    { Command c; REQUIRE(f.commands.tryPop(c));
      REQUIRE(c.kind == Command::Kind::LoadProject);
      delete c.payload.loadProject.config; }

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::path rplg(romPath); rplg.replace_extension(".rplg");
    std::filesystem::remove(rplg, ec);
}

// ---------------------------------------------------------------------------
// Project::loadFromConfig — the single shared apply path used by the DSP
// (applyProjectFromConfig), the UI test harness, and the CLI harness loadRplg.
// Regression: applying a loaded project used to reset config() to defaults and
// only re-add systems, so the saved zoom / layout / routing were dropped (a
// saved project reopened at the default zoom).
// ---------------------------------------------------------------------------
TEST_CASE("Project::loadFromConfig preserves settings and rebuilds systems",
          "[PluginRpcService][project-load]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    Project project;
    // Seed stale settings to prove loadFromConfig replaces (not merges) them.
    project.config().settings.zoom   = 1;
    project.config().settings.layout = SystemLayout::Row;

    ProjectConfig cfg;
    cfg.settings.zoom         = 5;
    cfg.settings.layout       = SystemLayout::Grid;
    cfg.settings.midiRouting  = MidiRouting::FourChannelsPerInstance;
    cfg.settings.audioRouting = AudioRouting::TwoPerInstance;
    SameBoyConfig sb{};
    sb.romPath  = kRomPath;
    sb.romBytes = rom;          // embedded so addSystem builds without disk
    cfg.systems.push_back(sb);

    const SystemId first = project.loadFromConfig(cfg);

    // The loaded project-wide settings were adopted (the regression).
    CHECK(project.config().settings.zoom         == 5);
    CHECK(project.config().settings.layout       == SystemLayout::Grid);
    CHECK(project.config().settings.midiRouting  == MidiRouting::FourChannelsPerInstance);
    CHECK(project.config().settings.audioRouting == AudioRouting::TwoPerInstance);
    // The system was rebuilt from the config.
    CHECK(first != 0);
    CHECK(project.systems().size() == 1);
    CHECK(project.findSystem(first) != nullptr);

    // Reloading a default config replaces the settings (zoom back to the 0 =
    // "inherit" sentinel) and clears the systems.
    project.loadFromConfig(ProjectConfig{});
    CHECK(project.config().settings.zoom   == 0);
    CHECK(project.config().settings.layout == SystemLayout::Auto);
    CHECK(project.systems().empty());
}
