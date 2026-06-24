// Tests for the recently-opened-projects list.
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

    REQUIRE(rf.add("/tmp/song.rplg"));
    REQUIRE(rf.add("/tmp/other.rplg"));

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == "/tmp/other.rplg");
    REQUIRE(snap[1].path == "/tmp/song.rplg");

    // File on disk reflects the same order.
    REQUIRE(fs::exists(dir / "recent.json"));
    auto parsed = recentFilesFromJson(slurp(dir / "recent.json"));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->entries.size() == 2);
    REQUIRE(parsed->entries[0].path == "/tmp/other.rplg");
    REQUIRE(parsed->entries[1].path == "/tmp/song.rplg");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles add() deduplicates by canonical path", "[recent-files]") {
    auto dir = makeTempDir("dedupe");

    // Touch a real file so weakly_canonical can resolve `./` -> absolute.
    const fs::path real = dir / "song.rplg";
    writeFile(real, "{}");

    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add(real.string()));
    REQUIRE(rf.add("/tmp/other.rplg"));
    // Re-add the same project under a non-canonical form. The dedupe should
    // collapse them and move the project back to the top.
    REQUIRE(rf.add((dir / "./song.rplg").string()));

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == real.string());
    REQUIRE(snap[1].path == "/tmp/other.rplg");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles add() trims to kMaxEntries", "[recent-files]") {
    auto dir = makeTempDir("trim");

    RecentFiles rf(dir);
    rf.start();

    // Push kMaxEntries + 3 unique paths; the oldest three should drop off.
    const std::size_t over = RecentFiles::kMaxEntries + 3;
    for (std::size_t i = 0; i < over; ++i) {
        const std::string p = "/tmp/proj_" + std::to_string(i) + ".rplg";
        REQUIRE(rf.add(p));
    }

    auto snap = rf.snapshot();
    REQUIRE(snap.size() == RecentFiles::kMaxEntries);
    // Newest first, so index 0 = the last add.
    REQUIRE(snap.front().path ==
            "/tmp/proj_" + std::to_string(over - 1) + ".rplg");
    // Oldest still in the list should be `over - kMaxEntries`.
    REQUIRE(snap.back().path ==
            "/tmp/proj_" + std::to_string(over - RecentFiles::kMaxEntries) + ".rplg");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles reloads list from recent.json across sessions", "[recent-files]") {
    auto dir = makeTempDir("reload");

    {
        RecentFiles rf(dir);
        rf.start();
        REQUIRE(rf.add("/tmp/a.rplg"));
        REQUIRE(rf.add("/tmp/b.rplg"));
    }

    RecentFiles rf2(dir);
    rf2.start();
    auto snap = rf2.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == "/tmp/b.rplg");
    REQUIRE(snap[1].path == "/tmp/a.rplg");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles tolerates malformed recent.json", "[recent-files]") {
    auto dir = makeTempDir("malformed");
    writeFile(dir / "recent.json", "{ this is not valid json");

    RecentFiles rf(dir);
    rf.start();
    REQUIRE(rf.snapshot().empty());

    // Subsequent add() should still work and overwrite the broken file.
    REQUIRE(rf.add("/tmp/fresh.rplg"));
    auto parsed = recentFilesFromJson(slurp(dir / "recent.json"));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->entries.size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles rejects empty paths", "[recent-files]") {
    auto dir = makeTempDir("empty-path");
    RecentFiles rf(dir);
    rf.start();

    REQUIRE_FALSE(rf.add(""));
    REQUIRE(rf.snapshot().empty());

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles remove() drops an entry and preserves order", "[recent-files]") {
    auto dir = makeTempDir("remove");
    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add("/tmp/a.rplg"));
    REQUIRE(rf.add("/tmp/b.rplg"));
    REQUIRE(rf.add("/tmp/c.rplg"));   // newest -> front: c, b, a

    REQUIRE(rf.remove("/tmp/b.rplg"));
    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == "/tmp/c.rplg");
    REQUIRE(snap[1].path == "/tmp/a.rplg");

    // Removing an absent path is a no-op.
    REQUIRE_FALSE(rf.remove("/tmp/missing.rplg"));
    REQUIRE(rf.snapshot().size() == 2);

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles remove() matches canonicalized paths", "[recent-files]") {
    auto dir = makeTempDir("remove-canon");
    const fs::path real = dir / "song.rplg";
    writeFile(real, "{}");

    RecentFiles rf(dir);
    rf.start();
    REQUIRE(rf.add(real.string()));

    // Remove via a non-canonical form of the same file.
    REQUIRE(rf.remove((dir / "./song.rplg").string()));
    REQUIRE(rf.snapshot().empty());

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles relink() replaces path in place", "[recent-files]") {
    auto dir = makeTempDir("relink");
    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add("/tmp/a.rplg"));
    REQUIRE(rf.add("/tmp/b.rplg"));
    REQUIRE(rf.add("/tmp/c.rplg"));   // c, b, a

    REQUIRE(rf.relink("/tmp/b.rplg", "/tmp/b2.rplg"));
    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 3);
    REQUIRE(snap[0].path == "/tmp/c.rplg");
    REQUIRE(snap[1].path == "/tmp/b2.rplg");   // same slot
    REQUIRE(snap[2].path == "/tmp/a.rplg");

    // Unknown oldPath -> false, no change.
    REQUIRE_FALSE(rf.relink("/tmp/nope.rplg", "/tmp/x.rplg"));
    REQUIRE(rf.snapshot().size() == 3);

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles relink() drops a colliding duplicate", "[recent-files]") {
    auto dir = makeTempDir("relink-collide");
    RecentFiles rf(dir);
    rf.start();

    REQUIRE(rf.add("/tmp/a.rplg"));
    REQUIRE(rf.add("/tmp/b.rplg"));
    REQUIRE(rf.add("/tmp/c.rplg"));   // c, b, a

    // Relinking b onto a's path collapses the two; the relinked entry is kept.
    REQUIRE(rf.relink("/tmp/b.rplg", "/tmp/a.rplg"));
    auto snap = rf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].path == "/tmp/c.rplg");
    REQUIRE(snap[1].path == "/tmp/a.rplg");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles rename() sets a sticky display alias", "[recent-files]") {
    auto dir = makeTempDir("rename");

    {
        RecentFiles rf(dir);
        rf.start();
        REQUIRE(rf.add("/tmp/song.rplg"));
        REQUIRE(rf.rename("/tmp/song.rplg", "My Song"));
        REQUIRE(rf.snapshot()[0].name == "My Song");

        // Re-adding the same path preserves the alias (sticky across reloads).
        REQUIRE(rf.add("/tmp/song.rplg"));
        REQUIRE(rf.snapshot()[0].name == "My Song");

        // A non-empty name on add overrides.
        REQUIRE(rf.add("/tmp/song.rplg", "Renamed"));
        REQUIRE(rf.snapshot()[0].name == "Renamed");

        // Renaming an absent path fails.
        REQUIRE_FALSE(rf.rename("/tmp/missing.rplg", "x"));
    }

    // Alias survives a fresh session.
    RecentFiles rf2(dir);
    rf2.start();
    REQUIRE(rf2.snapshot()[0].name == "Renamed");

    fs::remove_all(dir);
}

TEST_CASE("RecentFiles fires onChange on add / remove / relink / rename", "[recent-files]") {
    auto dir = makeTempDir("on-change");
    RecentFiles rf(dir);
    rf.start();

    int calls = 0;
    rf.setOnChange([&] { ++calls; });

    REQUIRE(rf.add("/tmp/a.rplg"));
    REQUIRE(rf.add("/tmp/b.rplg"));
    REQUIRE(rf.rename("/tmp/a.rplg", "Alias"));
    REQUIRE(rf.relink("/tmp/a.rplg", "/tmp/a2.rplg"));
    REQUIRE(rf.remove("/tmp/b.rplg"));
    REQUIRE(calls == 5);

    // No-op mutations don't fire the callback.
    REQUIRE_FALSE(rf.remove("/tmp/gone.rplg"));
    REQUIRE_FALSE(rf.relink("/tmp/gone.rplg", "/tmp/x.rplg"));
    REQUIRE_FALSE(rf.rename("/tmp/gone.rplg", "y"));
    REQUIRE(calls == 5);

    fs::remove_all(dir);
}
