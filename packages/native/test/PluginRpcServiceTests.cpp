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
#include "project/ProjectConfig.hpp"
#include "project/ProjectMissingFiles.hpp"
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

// Reproduce the shared-TS thin project save (projectSerialization.ts
// saveProjectFile) over the native byte-mover primitives. The .rplg save
// orchestration moved to TS, so a pure-C++ test drives the primitives directly:
// snapshotProjectConfig(baseDir) yields the thin config JSON (blobs stripped,
// asset paths rebased relative to the .rplg's dir, schema stamped); writeFile
// spills it; notifyProjectSaved does the post-save bookkeeping (recent +
// currentProjectPath + clear-dirty + emit "project-saved"). Mirrors runSave in
// packages/ui/src/project/projectHost.ts.
static void saveThinProject(PluginRpcService& service, const std::string& path) {
    const std::string baseDir = std::filesystem::path(path).parent_path().string();
    auto snap = service.snapshotProjectConfig(baseDir);
    REQUIRE(service.writeFile(
        path, std::vector<std::uint8_t>(snap.config.begin(), snap.config.end())));
    REQUIRE(service.notifyProjectSaved(path, /*exported*/ false));
}

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

TEST_CASE("PluginRpcService getProjectView fans in the individual getters",
          "[PluginRpcService]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    Fixture fx;

    // Seed non-default settings directly on the project (the DSP replica the
    // getters read) plus a focus, then assert the atomic view returns exactly
    // what the six per-field getters do — the UI relies on this one call
    // replacing that fan-out with a single, tear-free read.
    fx.project.config().settings.midiRouting  = MidiRouting::MidiChannelToInstance;
    fx.project.config().settings.audioRouting = AudioRouting::OnePerInstance;
    fx.project.config().settings.layout       = SystemLayout::Grid;
    fx.project.config().settings.zoom         = 4;
    fx.focused.store(fx.id);

    const auto v    = fx.service.getProjectView();
    const auto list = fx.service.listSystems();

    REQUIRE(v.systems.size() == list.size());
    REQUIRE(v.systems.size() == 1);
    CHECK(v.systems[0].id   == list[0].id);
    CHECK(v.systems[0].kind == list[0].kind);

    CHECK(v.focus        == fx.service.getFocus());
    CHECK(v.midiRouting  == fx.service.getMidiRouting());
    CHECK(v.audioRouting == fx.service.getAudioRouting());
    CHECK(v.layout       == fx.service.getLayout());
    CHECK(v.projectZoom  == fx.service.getProjectZoom());

    // And the concrete values we seeded round-trip through the view.
    CHECK(v.focus        == fx.id);
    CHECK(v.projectZoom  == 4u);
    CHECK(v.layout       == static_cast<std::uint32_t>(SystemLayout::Grid));
    CHECK(v.midiRouting  == static_cast<std::uint32_t>(MidiRouting::MidiChannelToInstance));
    CHECK(v.audioRouting == static_cast<std::uint32_t>(AudioRouting::OnePerInstance));
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
// tests (idle-tick writes are gated on userConfig_->sramMirror() == Continuous).
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

// Duplicating a system (or loading the same ROM twice) must hand each instance
// its own loose battery file, or both auto-save over one `<rom>.sav` and clobber
// each other. The duplicate gets suffix 2 -> `<rom>-2.sav`; a third gets 3.
TEST_CASE("duplicateSystem gives each instance a distinct sibling .sav",
          "[PluginRpcService][sram]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    PumpFixture f;
    auto base = makeTmpSys(f.project, rom, "dup");      // suffix 0 -> <rom>.sav
    CHECK(base.sys->savSuffix() == 0);

    REQUIRE(f.service.duplicateSystem(base.id));
    Command cmd;
    REQUIRE(f.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::AddSystem);
    auto* dup = dynamic_cast<SameBoySystem*>(cmd.payload.addSystem.newSystem);
    REQUIRE(dup != nullptr);

    CHECK(dup->romPath() == base.romPath);              // same ROM file...
    CHECK(dup->savSuffix() == 2);                       // ...but its own battery slot
    const std::string basePath =
        rp::sram_autosave::siblingSavPath(base.sys->romPath(), base.sys->savSuffix());
    const std::string dupPath =
        rp::sram_autosave::siblingSavPath(dup->romPath(), dup->savSuffix());
    CHECK(basePath != dupPath);                         // distinct targets
    CHECK(dupPath.find("-2.sav") != std::string::npos);

    // Behavioural proof: give the two instances *different* battery RAM, run each
    // instance's auto-save, and confirm two files survive on disk holding their
    // own contents — i.e. neither overwrote the other's `.sav`.
    std::error_code ec;
    std::filesystem::remove(dupPath, ec);               // base.savPath already cleared by makeTmpSys
    const std::size_t n = base.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> baseImg(n, 0x11);
    const std::vector<std::uint8_t> dupImg(n, 0x22);
    REQUIRE(base.sys->loadSramBytes(baseImg));
    REQUIRE(dup->loadSramBytes(dupImg));

    std::optional<std::uint64_t> hashBase, hashDup;
    REQUIRE(rp::autoSaveSramToSibling(*base.sys, hashBase));
    REQUIRE(rp::autoSaveSramToSibling(*dup, hashDup));
    REQUIRE(fileExists(basePath));
    REQUIRE(fileExists(dupPath));
    // Each file holds its own instance's SRAM, untouched by the other.
    CHECK(rp::sram_autosave::hashFile(basePath) ==
          rp::lsdj::SampleCache::hashBytes(baseImg.data(), baseImg.size()));
    CHECK(rp::sram_autosave::hashFile(dupPath) ==
          rp::lsdj::SampleCache::hashBytes(dupImg.data(), dupImg.size()));
    CHECK(rp::sram_autosave::hashFile(basePath) != rp::sram_autosave::hashFile(dupPath));

    // Adopt the duplicate, then duplicate the base again: the next free slot is 3
    // (0 and 2 are taken), not a re-collision on 2.
    f.project.adoptSystem(cmd.payload.addSystem.newSystem);
    REQUIRE(f.service.duplicateSystem(base.id));
    Command cmd2;
    REQUIRE(f.commands.tryPop(cmd2));
    REQUIRE(cmd2.kind == Command::Kind::AddSystem);
    auto* dup3 = dynamic_cast<SameBoySystem*>(cmd2.payload.addSystem.newSystem);
    REQUIRE(dup3 != nullptr);
    CHECK(dup3->savSuffix() == 3);
    delete cmd2.payload.addSystem.newSystem;

    std::filesystem::remove(basePath, ec);
    std::filesystem::remove(dupPath, ec);
}

// The reuse-after-remove hazard: add an instance (writes <rom>-2.sav), remove
// it, then duplicate again. Suffix 2 is free among live systems but its file
// still sits on disk holding the removed instance's save. Reclaiming 2 would
// have the duplicate (carrying the source's SRAM) auto-save over that orphan, so
// assignSavSuffix must skip to 3 and leave <rom>-2.sav byte-for-byte intact.
TEST_CASE("duplicate does not clobber a removed instance's orphaned .sav",
          "[PluginRpcService][sram]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    PumpFixture f;
    auto base = makeTmpSys(f.project, rom, "orphan");   // A: suffix 0
    const std::string dup2Path =
        rp::sram_autosave::siblingSavPath(base.romPath, 2);
    const std::string dup3Path =
        rp::sram_autosave::siblingSavPath(base.romPath, 3);
    std::error_code ec;
    std::filesystem::remove(dup2Path, ec);
    std::filesystem::remove(dup3Path, ec);

    // Duplicate A -> B (suffix 2), give B its own SRAM, adopt + auto-save it so
    // <rom>-2.sav lands on disk.
    REQUIRE(f.service.duplicateSystem(base.id));
    Command addB;
    REQUIRE(f.commands.tryPop(addB));
    auto* b = dynamic_cast<SameBoySystem*>(addB.payload.addSystem.newSystem);
    REQUIRE(b != nullptr);
    REQUIRE(b->savSuffix() == 2);
    const std::size_t n = b->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> bImg(n, 0x7E);
    REQUIRE(b->loadSramBytes(bImg));
    const SystemId bId = b->id();
    f.project.adoptSystem(addB.payload.addSystem.newSystem);
    std::optional<std::uint64_t> hb;
    REQUIRE(rp::autoSaveSramToSibling(*b, hb));
    REQUIRE(fileExists(dup2Path));
    const std::uint64_t orphanHash = rp::sram_autosave::hashFile(dup2Path);
    CHECK(orphanHash == rp::lsdj::SampleCache::hashBytes(bImg.data(), bImg.size()));

    // Remove B. <rom>-2.sav is now orphaned — no live system owns suffix 2.
    f.project.removeSystem(bId);

    // Duplicate A again -> C. Suffix 2 is free among live systems but the file
    // exists, so C must take 3, not clobber the orphan.
    REQUIRE(f.service.duplicateSystem(base.id));
    Command addC;
    REQUIRE(f.commands.tryPop(addC));
    auto* c = dynamic_cast<SameBoySystem*>(addC.payload.addSystem.newSystem);
    REQUIRE(c != nullptr);
    CHECK(c->savSuffix() == 3);
    CHECK(rp::sram_autosave::siblingSavPath(c->romPath(), c->savSuffix()) == dup3Path);
    // The orphan on disk is untouched.
    CHECK(fileExists(dup2Path));
    CHECK(rp::sram_autosave::hashFile(dup2Path) == orphanHash);
    delete addC.payload.addSystem.newSystem;

    std::filesystem::remove(base.savPath, ec);
    std::filesystem::remove(dup2Path, ec);
    std::filesystem::remove(dup3Path, ec);
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

    // Default (OnProjectSave) -> the idle pump never writes; that's the DSP
    // flush hook's job. Only Continuous mirrors on the idle tick.
    REQUIRE(fx.userConfig.sramMirror() == rp::SramMirror::OnProjectSave);
    fx.service.pumpSramAutoSave();
    CHECK_FALSE(fileExists(t.savPath));

    // Continuous -> the first pump flushes (the throttle starts disarmed).
    REQUIRE(fx.userConfig.setSramMirror(rp::SramMirror::Continuous));
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
    REQUIRE(fx.userConfig.setSramMirror(rp::SramMirror::Continuous));
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

// flushSramMirror is the loose-`.sav` spill the DSP calls at host save (getState)
// and quit (deactivate) — porting/23 D2/D4. Exercises the mode gate + on-disk
// dedup directly, since the DSP hooks themselves need a DPF host to reach.
TEST_CASE("flushSramMirror honours the mirror mode and dedups on disk",
          "[PluginRpcService][sram-autosave]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    Project project;
    auto t = makeTmpSys(project, rom, "flush");
    const std::size_t n = t.sys->saveSramBytes().size();
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> imageA(n, 0x5A);
    REQUIRE(t.sys->loadSramBytes(imageA));

    std::error_code ec;
    std::filesystem::remove(t.savPath, ec);

    // Off -> never touches the loose file.
    CHECK(rp::flushSramMirror(project.systems(), rp::SramMirror::Off) == 0);
    CHECK_FALSE(fileExists(t.savPath));

    // OnProjectSave -> writes the sibling (the DAW host-save / quit path).
    CHECK(rp::flushSramMirror(project.systems(), rp::SramMirror::OnProjectSave) == 1);
    REQUIRE(fileExists(t.savPath));
    CHECK(readFile(t.savPath) == imageA);

    // Unchanged SRAM -> a second flush is a no-op (dedup against the on-disk file;
    // each call starts from a fresh nullopt hash, so no throttle is involved).
    CHECK(rp::flushSramMirror(project.systems(), rp::SramMirror::OnProjectSave) == 0);

    // Changed SRAM -> flushed again (Continuous mirrors on save too).
    const std::vector<std::uint8_t> imageB(n, 0xA5);
    REQUIRE(t.sys->loadSramBytes(imageB));
    CHECK(rp::flushSramMirror(project.systems(), rp::SramMirror::Continuous) == 1);
    CHECK(readFile(t.savPath) == imageB);

    std::filesystem::remove(t.savPath, ec);
}

// The UI toggle persists the preference AND pushes the mode to the DSP (which
// reads it in its flush hooks). The pump also reconciles a drifted mode so a
// config.json edit converges the DSP within one idle tick.
TEST_CASE("setSramMirror persists the mode and pushes it to the DSP",
          "[PluginRpcService][sram]") {
    PumpFixture fx;

    auto drainLastMirror = [&]() -> std::optional<rp::SramMirror> {
        std::optional<rp::SramMirror> found;
        Command c;
        while (fx.commands.tryPop(c))
            if (c.kind == Command::Kind::SetSramMirror)
                found = c.payload.setSramMirror.mode;
        return found;
    };

    REQUIRE(fx.service.setSramMirror("Continuous"));
    REQUIRE(fx.userConfig.sramMirror() == rp::SramMirror::Continuous);
    auto pushed = drainLastMirror();
    REQUIRE(pushed.has_value());
    CHECK(*pushed == rp::SramMirror::Continuous);

    // An unrecognised mode name falls back to OnProjectSave (documented contract).
    REQUIRE(fx.service.setSramMirror("bogus"));
    CHECK(fx.userConfig.sramMirror() == rp::SramMirror::OnProjectSave);

    // A direct UserConfig change (as an efsw config.json edit would produce) is
    // reconciled to the DSP by the next pump.
    REQUIRE(fx.userConfig.setSramMirror(rp::SramMirror::Off));
    fx.service.pumpSramAutoSave();
    pushed = drainLastMirror();
    REQUIRE(pushed.has_value());
    CHECK(*pushed == rp::SramMirror::Off);
}

// D1: on load, embedded SRAM (the DAW chunk) is authoritative — the loose
// sibling on disk is never consulted when the config already carries bytes.
// Project::addSystem only slurps the sibling `if (cfg.sram.empty())`.
TEST_CASE("embedded SRAM wins over a conflicting on-disk sibling (chunk-authoritative)",
          "[PluginRpcService][sram]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();

    const std::string romPath = uniqueTmpPath("d1", ".gb");
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);

    // Probe the cart's battery size with a throwaway system.
    std::size_t n = 0;
    {
        SameBoyConfig c{}; c.romPath = romPath;
        SameBoySystem s{SystemId{77}, c, rom};
        s.onActivate(kSampleRate);
        n = s.saveSramBytes().size();
        s.onDeactivate();
    }
    REQUIRE(n > 0);

    const std::vector<std::uint8_t> embedded(n, 0x11);   // the "chunk" SRAM
    const std::vector<std::uint8_t> onDisk  (n, 0x22);   // a conflicting sibling
    {
        std::ofstream o(savPath, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(onDisk.data()),
                static_cast<std::streamsize>(onDisk.size()));
    }

    // Load with embedded bytes present: the sibling must be ignored entirely.
    Project project;
    SameBoyConfig cfg{};
    cfg.romPath  = romPath;
    cfg.romBytes = rom;
    cfg.sram     = embedded;
    const SystemId id = project.addSystem(cfg);
    REQUIRE(id != 0);
    SystemBase* sys = project.findSystem(id);
    REQUIRE(sys != nullptr);
    sys->onActivate(kSampleRate);
    CHECK(sys->saveSramBytes() == embedded);             // chunk wins, not onDisk
    sys->onDeactivate();

    std::error_code ec;
    std::filesystem::remove(savPath, ec);
}

// D5: sanitizeSavTargets clears a paired-save override whose directory is gone
// (a project moved between machines), so a later mirror flush falls back to the
// ROM sibling instead of a dangling absolute path. A target whose directory
// still exists is kept.
TEST_CASE("sanitizeSavTargets drops dangling paired-save write-targets",
          "[PluginRpcService][sram]") {
    ProjectConfig cfg;

    SameBoyConfig gone{};
    gone.romPath = "/nonexistent-machine/roms/game.gb";
    gone.savPath = "/nonexistent-machine/roms/game.sav";   // parent dir absent
    cfg.systems.push_back(SystemConfig{gone});

    const std::string liveDir  = uniqueTmpPath("d5live", "");   // a real directory
    std::filesystem::create_directories(liveDir);
    SameBoyConfig live{};
    live.romPath = liveDir + "/here.gb";
    live.savPath = liveDir + "/here.sav";                  // parent dir exists (kept)
    cfg.systems.push_back(SystemConfig{live});

    const int cleared = rp::sanitizeSavTargets(cfg);
    CHECK(cleared == 1);
    CHECK(rfl::get_if<SameBoyConfig>(&cfg.systems[0].variant())->savPath.empty());        // dropped
    CHECK(rfl::get_if<SameBoyConfig>(&cfg.systems[1].variant())->savPath == live.savPath); // kept

    std::error_code ec;
    std::filesystem::remove_all(liveDir, ec);
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

    // Save now runs in shared TS (snapshot -> write) and finishes with the
    // native notifyProjectSaved, which clears the dirty flag + emits
    // "project-saved". Drive the primitives directly (no JS runtime in a C++ test).
    const std::string proj = "/tmp/rpc_unsaved_proj.rplg";
    saveThinProject(fx.service, proj);
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

// Cartridge battery size for `rom` (0 if the cart has no battery). Lets a test
// author a correctly-sized `.sav` before loading.
std::size_t probeBatterySize(const std::vector<std::uint8_t>& rom) {
    SameBoyConfig c{};
    SameBoySystem s{SystemId{0xB0FF}, c, rom};
    s.onActivate(kSampleRate);
    const std::size_t n = s.saveSramBytes().size();
    s.onDeactivate();
    return n;
}

// A thin (path-only) project referencing `romPath`, as projectConfigToJsonFile
// produces. Used to pre-seed an existing sibling project on disk. `savPath`, when
// non-empty, sets the system's explicit paired-save override.
void writeThinProject(const std::string& path, const std::string& romPath,
                      const std::string& savPath = "") {
    SameBoyConfig sb{};
    sb.romPath = romPath;
    sb.savPath = savPath;
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

// Schema version: a project stamped newer than this build is refused with the
// "project-incompatible" event and never commits. projectConfigToJson (unlike
// ...ToJsonFile) does not re-stamp, so the forced future version survives to disk.
TEST_CASE("loadProjectFromPath refuses a project from a newer schema version",
          "[PluginRpcService][schema-version]") {
    RecentFixture fx;

    SameBoyConfig sb{}; sb.romPath = uniqueTmpPath("vernew", ".gb");
    ProjectConfig cfg; cfg.systems.push_back(sb);
    cfg.schemaVersion = std::to_string(rp::schema::kProject + 1);
    const std::string projPath = uniqueTmpPath("vernew", ".rplg");
    const std::string json = projectConfigToJson(cfg);
    writeBytes(projPath, std::vector<std::uint8_t>(json.begin(), json.end()));

    CHECK_FALSE(fx.service.loadProjectFromPath(projPath));   // refused at the gate
    CHECK(fx.sawEvent("project-incompatible"));
    CHECK_FALSE(fx.sawEvent("project-loaded"));

    std::error_code ec;
    std::filesystem::remove(projPath, ec);
}

// The gate only refuses *newer* — an older/equal version loads normally (older is
// the future-migration hook, not a rejection).
TEST_CASE("loadProjectFromPath accepts an older schema version",
          "[PluginRpcService][schema-version]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("verold", ".gb");
    writeBytes(romPath, fx.rom);
    SameBoyConfig sb{}; sb.romPath = romPath;
    ProjectConfig cfg; cfg.systems.push_back(sb);
    cfg.schemaVersion = "0";                                 // older than kProject
    const std::string projPath = uniqueTmpPath("verold", ".rplg");
    const std::string json = projectConfigToJson(cfg);
    writeBytes(projPath, std::vector<std::uint8_t>(json.begin(), json.end()));

    REQUIRE(fx.service.loadProjectFromPath(projPath));       // loads (ROM present)
    CHECK_FALSE(fx.sawEvent("project-incompatible"));
    CHECK(fx.sawEvent("project-loaded"));

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

TEST_CASE("scanMissingFiles treats an embedded-mGB system as present",
          "[PluginRpcService][mgb]") {
    // A saved mGB project is path-less and byte-less (the bytes live in the
    // binary). scanMissingFiles must treat the embeddedRom marker as present,
    // else loadProjectFromPath (standalone Load Project / recent / autoload)
    // would route it into the relink menu instead of loading it.
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.embeddedRom = "mgb";
    sb.embedRom    = false;                // no romBytes; no romPath
    cfg.systems.push_back(sb);
    CHECK(rp::scanMissingFiles(cfg).empty());

    // Sanity: a genuinely missing ROM (no marker, no bytes, bogus path) IS flagged.
    ProjectConfig bad;
    SameBoyConfig miss;
    miss.romPath = "/nonexistent/rp-does-not-exist.gb";
    bad.systems.push_back(miss);
    CHECK_FALSE(rp::scanMissingFiles(bad).empty());
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

// ---------------------------------------------------------------------------
// Pairing a user-picked `.sav` with a ROM in the Open browser.
// Drives the real onFileBrowserSelected round-trip; the fixture's browser
// callback is a no-op, so a 2nd (ROM) dialog is simulated by calling
// onFileBrowserSelected again.
// ---------------------------------------------------------------------------

TEST_CASE("pair: picking a .sav loads its sibling ROM (replace)",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("pair", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);
    const std::size_t n = probeBatterySize(fx.rom);
    REQUIRE(n > 0);
    const std::vector<std::uint8_t> image(n, 0x3C);
    writeBytes(savPath, image);
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    REQUIRE(fx.service.openRomBrowser({}));            // replace mode
    fx.service.onFileBrowserSelected(savPath.c_str()); // user picks the .sav

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == romPath);                  // paired with the sibling ROM
    CHECK(sb->saveSramBytes() == image);              // battery seeded from the sav
    CHECK(sb->savPath().empty());                     // it IS the sibling -> no override
    CHECK(fx.sawEvent("rom-loaded"));
    delete cmd.payload.loadRom.newSystem;

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

TEST_CASE("pair: picking a .sav adds an instance of its sibling ROM (add)",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("pairadd", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);
    const std::size_t n = probeBatterySize(fx.rom);
    const std::vector<std::uint8_t> image(n, 0x42);
    writeBytes(savPath, image);

    PluginRpcService::OpenRomOpts opts; opts.mode = "add";
    REQUIRE(fx.service.openRomBrowser(opts));
    fx.service.onFileBrowserSelected(savPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::AddSystem);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.addSystem.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == romPath);
    CHECK(sb->saveSramBytes() == image);
    CHECK(sb->savSuffix() == 0);                       // first instance of this ROM
    CHECK(sb->savPath().empty());                      // picked == sibling
    delete cmd.payload.addSystem.newSystem;

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
}

TEST_CASE("pair: a .sav with no sibling ROM opens a 2nd browser, then honours the override",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::size_t n = probeBatterySize(fx.rom);
    const std::vector<std::uint8_t> image(n, 0x5A);
    const std::string savPath = uniqueTmpPath("orphan", ".sav");  // no sibling ROM
    writeBytes(savPath, image);
    const std::string romPath = uniqueTmpPath("elsewhere", ".gb"); // different stem
    writeBytes(romPath, fx.rom);
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());   // no sibling -> arms 2nd browser
    CHECK_FALSE(fx.sawEvent("rom-loaded"));
    CHECK_FALSE(fx.sawEvent("rom-error"));
    Command none;
    CHECK_FALSE(fx.commands.tryPop(none));               // nothing queued yet

    fx.service.onFileBrowserSelected(romPath.c_str());   // user picks the ROM
    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == romPath);
    CHECK(sb->saveSramBytes() == image);                 // seeded from the orphan sav
    CHECK(sb->savPath() == savPath);                     // non-sibling -> override set
    CHECK(fx.sawEvent("rom-loaded"));

    // Auto-save writes back to the paired file, NOT <elsewhere>.sav.
    const std::string elsewhereSibling = rp::sram_autosave::siblingSavPath(romPath);
    std::filesystem::remove(elsewhereSibling, ec);
    const std::vector<std::uint8_t> played(n, 0x77);
    REQUIRE(sb->loadSramBytes(played));
    std::optional<std::uint64_t> h;
    REQUIRE(rp::autoSaveSramToSibling(*sb, h));
    CHECK(readFile(savPath) == played);                  // the picked file was updated
    CHECK_FALSE(fileExists(elsewhereSibling));           // the sibling was NOT written
    delete cmd.payload.loadRom.newSystem;

    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

TEST_CASE("pair: a <name>-N.sav auto-pairs with the <name> ROM (no 2nd browser)",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("song", ".gb");
    writeBytes(romPath, fx.rom);
    auto p = std::filesystem::path(romPath);
    const std::string savPath =
        (p.parent_path() / (p.stem().string() + "-2.sav")).string();  // the `-2` slot
    const std::size_t n = probeBatterySize(fx.rom);
    const std::vector<std::uint8_t> image(n, 0x2B);
    writeBytes(savPath, image);
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));                     // paired immediately, no 2nd browser
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == romPath);
    CHECK(sb->saveSramBytes() == image);
    CHECK(sb->savPath() == savPath);                     // `-2.sav` != `<rom>.sav` -> override
    delete cmd.payload.loadRom.newSystem;

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

TEST_CASE("pair: an exact <name>-N ROM beats the base ROM for a <name>-N.sav",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string basePath = uniqueTmpPath("tune", ".gb");
    writeBytes(basePath, fx.rom);
    auto p = std::filesystem::path(basePath);
    const std::string dupRom =
        (p.parent_path() / (p.stem().string() + "-2.gb")).string();
    const std::string savPath =
        (p.parent_path() / (p.stem().string() + "-2.sav")).string();
    writeBytes(dupRom, fx.rom);
    const std::size_t n = probeBatterySize(fx.rom);
    writeBytes(savPath, std::vector<std::uint8_t>(n, 0x19));
    std::error_code ec;
    std::filesystem::remove(siblingRplg(dupRom), ec);

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == dupRom);                      // exact `-2` stem, not the base
    delete cmd.payload.loadRom.newSystem;

    std::filesystem::remove(basePath, ec);
    std::filesystem::remove(dupRom, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(dupRom), ec);
}

TEST_CASE("pair: picking a ROM still loads normally",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("plain", ".gb");
    writeBytes(romPath, fx.rom);
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(romPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath() == romPath);
    CHECK(sb->savPath().empty());                        // plain load -> no override
    CHECK(fx.sawEvent("rom-loaded"));
    CHECK(fileExists(siblingRplg(romPath)));             // normal sibling-project write
    delete cmd.payload.loadRom.newSystem;

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

TEST_CASE("pair: an explicit sav pick bypasses a sibling .rplg",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("withproj", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);
    const std::size_t n = probeBatterySize(fx.rom);
    const std::vector<std::uint8_t> imageA(n, 0xA1);
    writeBytes(savPath, imageA);
    writeThinProject(siblingRplg(romPath), romPath);     // a project sits beside the ROM

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());

    // Picking the .sav pairs directly — it must NOT defer to the sibling project.
    CHECK(fx.sawEvent("rom-loaded"));
    CHECK_FALSE(fx.sawEvent("project-loaded"));
    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    CHECK(sb->saveSramBytes() == imageA);
    delete cmd.payload.loadRom.newSystem;

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

TEST_CASE("pair: duplicating a paired system does not inherit its sav file",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("paired", ".gb");
    SameBoyConfig cfg{};
    cfg.romPath = romPath;
    cfg.savPath = uniqueTmpPath("explicit", ".sav");     // an explicit paired file
    const SystemId id = fx.project.nextSystemId();
    auto owned = std::make_unique<SameBoySystem>(id, cfg, fx.rom);
    owned->onActivate(kSampleRate);
    REQUIRE(owned->savPath() == cfg.savPath);
    fx.project.adoptSystem(owned.release());

    REQUIRE(fx.service.duplicateSystem(id));
    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::AddSystem);
    auto* dup = dynamic_cast<SameBoySystem*>(cmd.payload.addSystem.newSystem);
    REQUIRE(dup != nullptr);
    CHECK(dup->savPath().empty());                       // NOT the source's paired file
    CHECK(dup->savSuffix() == 2);                        // disambiguated instead
    delete cmd.payload.addSystem.newSystem;
}

TEST_CASE("pair: a non-ROM picked in the 2nd browser errors, loads nothing",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::size_t n = probeBatterySize(fx.rom);
    const std::string savPath = uniqueTmpPath("lonely", ".sav");   // no sibling ROM
    writeBytes(savPath, std::vector<std::uint8_t>(n, 0x33));

    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());   // arms 2nd browser

    const std::string notRom = uniqueTmpPath("notrom", ".sav");    // 2nd pick isn't a ROM
    writeBytes(notRom, std::vector<std::uint8_t>(n, 0x44));
    fx.service.onFileBrowserSelected(notRom.c_str());

    CHECK(fx.sawEvent("rom-error"));
    Command none;
    CHECK_FALSE(fx.commands.tryPop(none));

    std::error_code ec;
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(notRom, ec);
}

// ---------------------------------------------------------------------------
// Missing-file relink for an explicit paired save (`savPath`).
// ---------------------------------------------------------------------------

TEST_CASE("relink: a missing paired savPath is flagged as an sram item",
          "[PluginRpcService][relink]") {
    const std::string missingSav = "/nonexistent/rp-relink-missing.sav";

    // A present ROM (embedded bytes) + an explicit savPath that isn't on disk.
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.romBytes = {1, 2, 3};          // romPresent via bytes — isolates the sram case
    sb.savPath  = missingSav;
    cfg.systems.push_back(sb);
    const auto missing = rp::scanMissingFiles(cfg);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].itemKind   == "sram");
    CHECK(missing[0].path       == missingSav);
    CHECK(missing[0].systemIndex == 0);

    // Negatives: no override, an existing override, and embedded SRAM are all "present".
    ProjectConfig none;      SameBoyConfig a; a.romBytes = {1}; a.savPath.clear();
    none.systems.push_back(a);
    CHECK(rp::scanMissingFiles(none).empty());

    const std::string realSav = uniqueTmpPath("relink_present", ".sav");
    writeBytes(realSav, {0, 0, 0});
    ProjectConfig present;   SameBoyConfig b; b.romBytes = {1}; b.savPath = realSav;
    present.systems.push_back(b);
    CHECK(rp::scanMissingFiles(present).empty());

    ProjectConfig embedded;  SameBoyConfig c; c.romBytes = {1}; c.savPath = missingSav; c.sram = {9, 9};
    embedded.systems.push_back(c);
    CHECK(rp::scanMissingFiles(embedded).empty());   // sram bytes in hand -> not missing

    std::error_code ec;
    std::filesystem::remove(realSav, ec);
}

TEST_CASE("relink: relinkInConfig sram sets savPath and leaves romPath",
          "[PluginRpcService][relink]") {
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.romPath = "/orig/rom.gb";
    sb.romBytes = {1};
    sb.savPath = "/old/save.sav";
    cfg.systems.push_back(sb);

    rp::MissingFile item{0, "sram", "/old/save.sav", -1, -1};
    REQUIRE(rp::relinkInConfig(cfg, item, "/located/save.sav"));

    const auto* out = rfl::get_if<SameBoyConfig>(&cfg.systems[0].variant());
    REQUIRE(out != nullptr);
    CHECK(out->savPath == "/located/save.sav");   // override repointed
    CHECK(out->romPath == "/orig/rom.gb");         // ROM untouched (the kitSlot<0 regression)
}

TEST_CASE("relink: loading a project with a missing paired save relinks it",
          "[PluginRpcService][relink]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    // Present ROM on disk + a thin project pointing at a missing paired save.
    const std::string romPath = uniqueTmpPath("relinksav", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string missingSav = uniqueTmpPath("relinksav_gone", ".sav");
    std::error_code ec;
    std::filesystem::remove(missingSav, ec);          // ensure it's absent
    const std::string projPath = uniqueTmpPath("relinksav_proj", ".rplg");
    writeThinProject(projPath, romPath, missingSav);

    // Load is held: only the sram is missing (ROM is present).
    REQUIRE(fx.service.loadProjectFromPath(projPath));
    CHECK(fx.sawEvent("missing-files"));
    CHECK_FALSE(fx.sawEvent("project-loaded"));
    auto missing = fx.service.getMissingFiles();
    REQUIRE(missing.items.size() == 1);
    CHECK(missing.items[0].itemKind == "sram");

    // Locate the save; the load commits and the queued project carries the path.
    const std::string realSav = uniqueTmpPath("relinksav_found", ".sav");
    writeBytes(realSav, {0x11, 0x22, 0x33});
    auto remaining = fx.service.relinkMissingFile(0, "sram", -1, -1, realSav);
    CHECK(remaining.items.empty());
    CHECK(fx.sawEvent("project-loaded"));

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadProject);
    const auto* sb = rfl::get_if<SameBoyConfig>(&cmd.payload.loadProject.config->systems.at(0).variant());
    REQUIRE(sb != nullptr);
    CHECK(sb->savPath == realSav);                    // committed project points at the located save
    delete cmd.payload.loadProject.config;

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(realSav, ec);
    std::filesystem::remove(projPath, ec);
}

// ---------------------------------------------------------------------------
// Portable .rplg: file saves store project-relative paths; loads resolve them.
// ---------------------------------------------------------------------------

TEST_CASE("paths: saveProjectToPath stores a ROM under the project dir as relative",
          "[PluginRpcService][paths]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;
    auto t = makeTmpSys(fx.project, fx.rom, "paths");   // ROM at a /tmp path
    const std::string projPath = uniqueTmpPath("paths_proj", ".rplg");   // same dir (/tmp)

    // Save = shared TS over the native primitives (snapshotProjectConfig rebases
    // the ROM path relative to the .rplg's dir); drive them directly here.
    saveThinProject(fx.service, projPath);
    REQUIRE(fx.sawEvent("project-saved"));

    const auto bytes = readFile(projPath);
    auto cfg = projectConfigFromBytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    REQUIRE(cfg.has_value());
    const auto* sb = rfl::get_if<SameBoyConfig>(&cfg->systems.at(0).variant());
    REQUIRE(sb != nullptr);
    CHECK_FALSE(std::filesystem::path(sb->romPath).is_absolute());   // stored relative
    CHECK(sb->romPath == std::filesystem::path(t.romPath).filename().string());  // bare basename

    std::error_code ec;
    std::filesystem::remove(t.romPath, ec);
    std::filesystem::remove(projPath, ec);
}

TEST_CASE("paths: a saved project folder can be moved and still loads",
          "[PluginRpcService][paths]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;
    std::error_code ec;

    // Dir A: a real ROM + a project referencing it.
    const auto dirA = std::filesystem::temp_directory_path() /
        ("rp_paths_A_" + std::to_string(processToken()) + "_" + std::to_string(g_tmpCounter.fetch_add(1)));
    std::filesystem::create_directories(dirA);
    const std::string romA  = (dirA / "game.gb").string();
    const std::string projA = (dirA / "proj.rplg").string();
    writeBytes(romA, fx.rom);
    {
        SameBoyConfig cfg; cfg.romPath = romA;
        const SystemId id = fx.project.nextSystemId();
        auto sys = std::make_unique<SameBoySystem>(id, cfg, fx.rom);
        sys->onActivate(kSampleRate);
        fx.project.adoptSystem(sys.release());
    }
    saveThinProject(fx.service, projA);
    REQUIRE(fx.sawEvent("project-saved"));

    // "Move" the folder: copy both files into a fresh dir B, then delete A so
    // only the relative path (game.gb) — not any absolute A path — can resolve.
    const auto dirB = std::filesystem::temp_directory_path() /
        ("rp_paths_B_" + std::to_string(processToken()) + "_" + std::to_string(g_tmpCounter.fetch_add(1)));
    std::filesystem::create_directories(dirB);
    std::filesystem::copy_file(projA, dirB / "proj.rplg");
    std::filesystem::copy_file(romA,  dirB / "game.gb");
    std::filesystem::remove_all(dirA, ec);

    fx.drainSystems();   // clear the SaveProject/queue state from the setup

    const std::string projB = (dirB / "proj.rplg").string();
    REQUIRE(fx.service.loadProjectFromPath(projB));
    CHECK_FALSE(fx.sawEvent("missing-files"));   // relative path resolved in dir B
    CHECK(fx.sawEvent("project-loaded"));

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadProject);
    const auto* sb = rfl::get_if<SameBoyConfig>(&cmd.payload.loadProject.config->systems.at(0).variant());
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath == std::filesystem::weakly_canonical(dirB / "game.gb").string());
    delete cmd.payload.loadProject.config;

    std::filesystem::remove_all(dirB, ec);
}

TEST_CASE("paths: the sibling .rplg records the ROM by relative basename",
          "[PluginRpcService][paths]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("paths_sib", ".gb");
    writeBytes(romPath, fx.rom);
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    REQUIRE(fx.service.loadRomFromPath(romPath));
    REQUIRE(fileExists(siblingRplg(romPath)));

    const auto bytes = readFile(siblingRplg(romPath));
    auto cfg = projectConfigFromBytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    REQUIRE(cfg.has_value());
    const auto* sb = rfl::get_if<SameBoyConfig>(&cfg->systems.at(0).variant());
    REQUIRE(sb != nullptr);
    CHECK(sb->romPath == std::filesystem::path(romPath).filename().string());

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

// ---------------------------------------------------------------------------
// Regressions found by the robustness audit.
// ---------------------------------------------------------------------------

// A cart whose battery lives inside an embedded savestate (zip form) is present,
// even with a stale explicit savPath — no phantom "missing save" that blocks load.
TEST_CASE("relink: an embedded savestate counts the battery as present",
          "[PluginRpcService][relink]") {
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.romBytes  = {1};                                 // ROM present (bytes)
    sb.savPath   = "/nonexistent/rp-phantom.sav";       // explicit override, absent
    sb.sram.clear();                                    // no standalone SRAM
    sb.savestate = {9, 9, 9};                           // battery carried in the savestate
    cfg.systems.push_back(sb);
    CHECK(rp::scanMissingFiles(cfg).empty());
}

// One system missing BOTH its ROM and its paired save must yield two items whose
// only distinguishing field is itemKind (kitSlot/sampleIndex both default -1) —
// the reason the RelinkMenu row key must include itemKind.
TEST_CASE("relink: a system missing both ROM and paired save yields two distinct-kind items",
          "[PluginRpcService][relink]") {
    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.romPath = "/nonexistent/rp-gone.gb";
    sb.savPath = "/nonexistent/rp-gone.sav";
    cfg.systems.push_back(sb);

    const auto missing = rp::scanMissingFiles(cfg);
    REQUIRE(missing.size() == 2);
    CHECK(missing[0].systemIndex == missing[1].systemIndex);
    CHECK(missing[0].kitSlot     == missing[1].kitSlot);      // both -1
    CHECK(missing[0].sampleIndex == missing[1].sampleIndex);  // both -1
    CHECK(missing[0].itemKind    != missing[1].itemKind);     // ONLY itemKind differs
    const bool rom  = missing[0].itemKind == "rom"  || missing[1].itemKind == "rom";
    const bool sram = missing[0].itemKind == "sram" || missing[1].itemKind == "sram";
    CHECK(rom);
    CHECK(sram);
}

// Add-pairing a ROM's own sibling <rom>.sav must NOT make the new instance share
// that file with the live suffix-0 instance — it gets its own <rom>-2.sav (seeded
// from the pick), no override.
TEST_CASE("pair: add-pairing a ROM's own sibling .sav gives the new instance its own file",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("collide", ".gb");
    writeBytes(romPath, fx.rom);
    const std::string savPath = rp::sram_autosave::siblingSavPath(romPath);   // <rom>.sav
    const std::size_t n = probeBatterySize(fx.rom);
    writeBytes(savPath, std::vector<std::uint8_t>(n, 0x01));
    std::error_code ec;
    std::filesystem::remove(siblingRplg(romPath), ec);

    // A live instance already owns <rom>.sav (suffix 0, no override).
    SameBoyConfig cfg; cfg.romPath = romPath;
    const SystemId liveId = fx.project.nextSystemId();
    auto live = std::make_unique<SameBoySystem>(liveId, cfg, fx.rom);
    live->onActivate(kSampleRate);
    SameBoySystem* livePtr = live.get();
    fx.project.adoptSystem(live.release());
    REQUIRE(rp::sram_autosave::resolveSavPath(*livePtr) == savPath);

    // Add-pair by picking <rom>.sav in the Open browser (add mode).
    PluginRpcService::OpenRomOpts opts; opts.mode = "add";
    REQUIRE(fx.service.openRomBrowser(opts));
    fx.service.onFileBrowserSelected(savPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::AddSystem);
    auto* dup = dynamic_cast<SameBoySystem*>(cmd.payload.addSystem.newSystem);
    REQUIRE(dup != nullptr);
    CHECK(dup->savPath().empty());                       // no override pinned
    CHECK(dup->savSuffix() == 2);                        // disambiguated instead
    const std::string dupTarget = rp::sram_autosave::resolveSavPath(*dup);
    CHECK(dupTarget != savPath);                         // NOT the live instance's file
    CHECK(dupTarget == rp::sram_autosave::siblingSavPath(romPath, 2));
    delete cmd.payload.addSystem.newSystem;

    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}

// Replace-mode pairing when a sibling <rom>.rplg already exists: writeSiblingProject
// won't overwrite it, so the freshly-pinned override isn't on disk. The project
// must stay DIRTY so a save/close persists the pairing (audit bug #2).
TEST_CASE("pair: replace-mode pairing over a stale sibling .rplg keeps the project dirty",
          "[PluginRpcService][pair]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    RecentFixture fx;

    const std::string romPath = uniqueTmpPath("staleproj", ".gb");
    writeBytes(romPath, fx.rom);
    // A pre-existing sibling project WITHOUT any override.
    writeThinProject(siblingRplg(romPath), romPath);
    // A non-sibling paired save (a `-2` name so findSiblingRom auto-pairs to romPath).
    auto p = std::filesystem::path(romPath);
    const std::string savPath = (p.parent_path() / (p.stem().string() + "-2.sav")).string();
    const std::size_t n = probeBatterySize(fx.rom);
    writeBytes(savPath, std::vector<std::uint8_t>(n, 0x5C));

    // Replace-mode pairing: picking the `-2.sav` auto-pairs with romPath.
    REQUIRE(fx.service.openRomBrowser({}));
    fx.service.onFileBrowserSelected(savPath.c_str());

    Command cmd;
    REQUIRE(fx.commands.tryPop(cmd));
    REQUIRE(cmd.kind == Command::Kind::LoadRom);
    auto* sb = dynamic_cast<SameBoySystem*>(cmd.payload.loadRom.newSystem);
    REQUIRE(sb != nullptr);
    REQUIRE(sb->savPath() == savPath);                     // override pinned in memory

    // Stale sibling on disk lacks the override -> the project MUST be dirty.
    CHECK(fx.service.getUnsavedSummary().project);

    const auto bytes = readFile(siblingRplg(romPath));
    auto cfg = projectConfigFromBytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    REQUIRE(cfg.has_value());
    const auto* stored = rfl::get_if<SameBoyConfig>(&cfg->systems.at(0).variant());
    REQUIRE(stored != nullptr);
    CHECK(stored->savPath.empty());                        // proves why dirty is needed

    delete cmd.payload.loadRom.newSystem;
    std::error_code ec;
    std::filesystem::remove(romPath, ec);
    std::filesystem::remove(savPath, ec);
    std::filesystem::remove(siblingRplg(romPath), ec);
}
