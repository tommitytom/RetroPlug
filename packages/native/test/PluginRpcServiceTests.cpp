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
#include <string>
#include <vector>

#include "PluginRpcService.hpp"
#include "project/Project.hpp"
#include "system/MemoryType.hpp"
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
