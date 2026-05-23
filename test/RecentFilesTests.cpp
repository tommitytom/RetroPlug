// Tests for the recently-loaded-files list.
//
// RecentFiles is UI-thread-owned (no efsw watcher), so every reload is
// synchronous through start() — no waitFor helpers needed.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "config/RecentFiles.hpp"
#include "config/RecentFilesSerialization.hpp"

namespace fs = std::filesystem;

namespace {

fs::path makeTempDir(const std::string& tag) {
    auto base = fs::temp_directory_path();
    std::random_device rd;
    auto dir = base / ("retroplug-recent-" + tag + "-" +
                       std::to_string(rd()));
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    auto size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0);
    std::string out(static_cast<size_t>(size), '\0');
    in.read(out.data(), size);
    return out;
}

void writeFile(const fs::path& p, const std::string& contents) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

} // namespace

TEST_CASE("RecentFiles starts empty when no file exists", "[recent-files]") {
    auto dir = makeTempDir("empty");

    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.snapshot().empty());

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles add() prepends and writes recent.json", "[recent-files]") {
    auto dir = makeTempDir("add");

    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add("/tmp/song.rplg", "project"));
    REQUIRE(rf.add("/tmp/lsdj.gb",   "rom"));

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].kind == "rom");
    REQUIRE(snap[1].kind == "project");

    // File on disk reflects the same order.
    REQUIRE(fs::exists(dir / "recent.json"));
    auto parsed = recentFilesFromJson(slurp(dir / "recent.json"));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->entries.size() == 2);
    REQUIRE(parsed->entries[0].kind == "rom");
    REQUIRE(parsed->entries[1].kind == "project");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles add() deduplicates by canonical path", "[recent-files]") {
    auto dir = makeTempDir("dedupe");

    // Touch a real file so weakly_canonical can resolve `./` -> absolute.
    const fs::path real = dir / "song.rplg";
    writeFile(real, "{}");

    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add(real.string(), "project"));
    REQUIRE(rf.add("/tmp/other.gb", "rom"));
    // Re-add the same project under a non-canonical form. The dedupe should
    // collapse them and move the project back to the top.
    REQUIRE(rf.add((dir / "./song.rplg").string(), "project"));

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].kind == "project");
    REQUIRE(snap[1].path == "/tmp/other.gb");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles add() trims to kMaxEntries", "[recent-files]") {
    auto dir = makeTempDir("trim");

    RecentFiles rf(dir);
    rf.start();

    // Push kMaxEntries + 3 unique paths; the oldest three should drop off.
    const std::size_t over = RecentFiles::kMaxEntries + 3;
    for (std::size_t i = 0; i < over; ++i) {
        const std::string p = "/tmp/rom_" + std::to_string(i) + ".gb";
        REQUIRE(rf.add(p, "rom"));
    }

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == RecentFiles::kMaxEntries);
    // Newest first, so index 0 = the last add.
    REQUIRE(snap.front().path ==
            "/tmp/rom_" + std::to_string(over - 1) + ".gb");
    // Oldest still in the list should be `over - kMaxEntries`.
    REQUIRE(snap.back().path ==
            "/tmp/rom_" + std::to_string(over - RecentFiles::kMaxEntries) + ".gb");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles reloads list from recent.json across sessions", "[recent-files]") {
    auto dir = makeTempDir("reload");

    {
        RecentFiles rf(dir);
        rf.start();
        REQUIRE(rf.add("/tmp/a.gb",     "rom"));
        REQUIRE(rf.add("/tmp/b.rplg",   "project"));
    }

    RecentFiles rf2(dir);
    rf2.start();
    auto snap = rf2.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == "/tmp/b.rplg");
    REQUIRE(snap[0].kind == "project");
    REQUIRE(snap[1].path == "/tmp/a.gb");
    REQUIRE(snap[1].kind == "rom");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles tolerates malformed recent.json", "[recent-files]") {
    auto dir = makeTempDir("malformed");
    writeFile(dir / "recent.json", "{ this is not valid json");

    RecentFiles rf(dir);
    rf.start();
    REQUIRE(rf.snapshot().empty());

    // Subsequent add() should still work and overwrite the broken file.
    REQUIRE(rf.add("/tmp/fresh.gb", "rom"));
    auto parsed = recentFilesFromJson(slurp(dir / "recent.json"));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->entries.size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles rejects unknown kinds", "[recent-files]") {
    auto dir = makeTempDir("kind");
    RecentFiles rf(dir);
    rf.start();

    REQUIRE_FALSE(rf.add("/tmp/x", "bogus"));
    REQUIRE_FALSE(rf.add("",       "rom"));
    REQUIRE(rf.snapshot().empty());

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles fires onChange callback on add", "[recent-files]") {
    auto dir = makeTempDir("on-change");
    RecentFiles rf(dir);
    rf.start();

    int calls = 0;
    rf.setOnChange([&] { ++calls; });

    REQUIRE(rf.add("/tmp/a.gb", "rom"));
    REQUIRE(rf.add("/tmp/b.gb", "rom"));
    REQUIRE(calls == 2);

    fs::remove_all(dir);
}
