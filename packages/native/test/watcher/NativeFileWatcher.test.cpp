// NativeFileWatcher — the efsw mechanism behind HostRpcService::drainChangedPaths (spec/07). Verifies
// it reports config.json + bindings/*.json (recursive config-dir watch) and registered ROMs (parent-dir
// watch, filtered by path), and that unregistered files under the config dir are ignored.
//
// efsw delivers on a background thread with real filesystem latency, so every assertion polls
// drainChangedPaths() with a deadline rather than reading once.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_set>

#include "host/rpc/HostRpcService.hpp"
#include "host/rpc/NativeFileWatcher.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

fs::path canon(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

// A unique scratch dir under temp (no Math.random in this env, so derive from the steady clock).
fs::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() / ("rp-watcher-" + std::to_string(stamp));
    fs::create_directories(dir);
    return dir;
}

// Poll drainChangedPaths, accumulating everything seen, until `want` (canonical) shows up or the
// deadline passes. Returns true if it arrived.
bool waitFor(NativeFileWatcher& w, const fs::path& want, std::chrono::milliseconds timeout = 5s) {
    const std::string target = canon(want).string();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unordered_set<std::string> seen;
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& p : w.drainChangedPaths()) seen.insert(canon(p).string());
        if (seen.count(target)) return true;
        std::this_thread::sleep_for(25ms);
    }
    return false;
}

// Drain for a fixed window and report whether `unwanted` (canonical) ever appeared.
bool sawWithin(NativeFileWatcher& w, const fs::path& unwanted, std::chrono::milliseconds window = 1s) {
    const std::string target = canon(unwanted).string();
    const auto deadline = std::chrono::steady_clock::now() + window;
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& p : w.drainChangedPaths())
            if (canon(p).string() == target) return true;
        std::this_thread::sleep_for(25ms);
    }
    return false;
}

} // namespace

TEST_CASE("NativeFileWatcher reports config.json and bindings profiles", "[watcher]") {
    const fs::path dir = makeTempDir();
    fs::create_directories(dir / "bindings");  // exists before the watcher so the recursive watch covers it

    NativeFileWatcher w(dir.string());
    std::this_thread::sleep_for(100ms);  // let the watch thread arm

    writeFile(dir / "config.json", "{}");
    CHECK(waitFor(w, dir / "config.json"));

    writeFile(dir / "bindings" / "wasd.json", "{}");
    CHECK(waitFor(w, dir / "bindings" / "wasd.json"));

    // An unrelated file under the config dir is watched by efsw but filtered out by the watcher.
    writeFile(dir / "recent.json", "[]");
    CHECK_FALSE(sawWithin(w, dir / "recent.json"));

    fs::remove_all(dir);
}

TEST_CASE("NativeFileWatcher reports a registered ROM change, ignores an unregistered sibling", "[watcher]") {
    const fs::path cfg  = makeTempDir();
    const fs::path roms = makeTempDir();
    writeFile(roms / "game.gb", "rom-v1");
    writeFile(roms / "other.gb", "rom-v1");

    NativeFileWatcher w(cfg.string());
    w.setWatchedRoms({(roms / "game.gb").string()});
    std::this_thread::sleep_for(100ms);

    writeFile(roms / "game.gb", "rom-v2");         // registered → reported
    CHECK(waitFor(w, roms / "game.gb"));

    writeFile(roms / "other.gb", "rom-v2");        // same dir, not registered → ignored
    CHECK_FALSE(sawWithin(w, roms / "other.gb"));

    // Unregistering stops the reports.
    w.setWatchedRoms({});
    std::this_thread::sleep_for(100ms);
    (void)w.drainChangedPaths();
    writeFile(roms / "game.gb", "rom-v3");
    CHECK_FALSE(sawWithin(w, roms / "game.gb"));

    fs::remove_all(cfg);
    fs::remove_all(roms);
}

// The HostRpcService facade the plugin actually uses: enableWatching() spins up the watcher, and
// drainChangedPaths / setWatchedRoms delegate to it. Before enableWatching they're inert (matching the
// test host / CLI, which never enable watching), so this also pins the opt-in contract.
TEST_CASE("HostRpcService gates the watcher behind enableWatching", "[watcher]") {
    const fs::path cfg  = makeTempDir();
    const fs::path roms = makeTempDir();
    writeFile(roms / "game.gb", "rom-v1");

    HostRpcService svc;
    // Inert until enabled.
    CHECK(svc.drainChangedPaths().empty());
    CHECK_FALSE(svc.setWatchedRoms({(roms / "game.gb").string()}));

    svc.enableWatching(cfg.string());
    CHECK(svc.setWatchedRoms({(roms / "game.gb").string()}));
    std::this_thread::sleep_for(100ms);

    writeFile(roms / "game.gb", "rom-v2");
    writeFile(cfg / "config.json", "{}");

    const std::string wantRom = canon(roms / "game.gb").string();
    const std::string wantCfg = canon(cfg / "config.json").string();
    std::unordered_set<std::string> seen;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && !(seen.count(wantRom) && seen.count(wantCfg))) {
        for (auto& p : svc.drainChangedPaths()) seen.insert(canon(p).string());
        std::this_thread::sleep_for(25ms);
    }
    CHECK(seen.count(wantRom));
    CHECK(seen.count(wantCfg));

    fs::remove_all(cfg);
    fs::remove_all(roms);
}
