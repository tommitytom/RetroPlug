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

#include "PluginRpcService.hpp"
#include "config/UserConfig.hpp"
#include "project/Project.hpp"
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
        service.setOpenFileBrowserCallback([](const char*, bool, const char*) {});
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

std::string uniqueTmpPath(const char* tag, const char* ext) {
    const int n = g_tmpCounter.fetch_add(1);
    auto p = std::filesystem::temp_directory_path() /
             (std::string("rp_as_") + tag + "_" + std::to_string(n) + ext);
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
                 ("rp_as_cfg_" + std::to_string(g_tmpCounter.fetch_add(1)));
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
