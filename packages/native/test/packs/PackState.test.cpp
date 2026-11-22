// state.json and the version locks (docs/downloadable-packs-loading-design.md section 1): the state round-trips and is
// replaced atomically, a newer one is left alone, concurrent writers each land, a version folder is in use while a
// process holds it, and a launch settles pending updates and removals. Every case runs on a TempDir.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "PackTestSupport.hpp"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackState.hpp"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::TempDir;

namespace {

void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("state.json round-trips, is replaced atomically, and a newer one is left alone", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    // No file is an empty state.
    auto empty = readPackState(root, &why);
    REQUIRE(empty);
    CHECK(empty->packs.empty());

    PackState state;
    state.packs.push_back({"mesen", "1.2.0", "1.3.0", false});
    state.packs.push_back({"risa", "2.3.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    const auto back = readPackState(root, &why);
    INFO(why);
    REQUIRE(back);
    REQUIRE(back->packs.size() == 2);
    CHECK(back->find("mesen")->pending == "1.3.0");
    CHECK(back->find("risa")->remove);
    // Nothing but state.json and the lock every access holds is left beside it: the temporary file was renamed over it.
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(root)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"state.json", "state.lock"});

    // A newer RetroPlug's file is refused and never overwritten.
    const std::string newer = R"({"version": 99, "packs": []})";
    writeText(root / "state.json", newer);
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));
    CHECK_FALSE(updatePackState(root, [](PackState&) { return true; }, &why));
    CHECK(readText(root / "state.json") == newer);
    // Read for its version first: a newer schema that does not parse as this one is still newer, not malformed.
    writeText(root / "state.json", R"({"version": 2, "packs": "a newer shape"})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));

    // And a malformed entry is refused, naming it.
    writeText(root / "state.json", R"({"version": 1, "packs": [{"id": "Bad Id", "active": "1.0.0"}]})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("is not a pack id"));
}

TEST_CASE("updatePackState serialises writers: every change lands", "[packs-folders]") {
    // Each thread takes the exclusive lock on its own open of state.lock, as a second process would, so none of the
    // read-modify-writes overwrites another's.
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i)
        writers.emplace_back([&root, i] {
            std::string why;
            updatePackState(
                root,
                [i](PackState& s) {
                    s.packs.push_back({"pack-" + std::to_string(i), "1.0.0", "", false});
                    return true;
                },
                &why);
        });
    for (auto& t : writers) t.join();
    std::string why;
    const auto state = readPackState(root, &why);
    REQUIRE(state);
    CHECK(state->packs.size() == 8);
}

TEST_CASE("a version folder is in use while a process holds it", "[packs-folders]") {
    TempDir tmp;
    const fs::path version = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    CHECK_FALSE(versionInUse(version));  // no lock file yet: nobody has loaded it
    std::string why;
    {
        auto held = holdVersion(version, &why);
        INFO(why);
        REQUIRE(held);
        CHECK(versionInUse(version));
        auto second = holdVersion(version, &why);  // a second process loading the same version shares it
        CHECK(second);
    }
    CHECK_FALSE(versionInUse(version));
}

TEST_CASE("a launch settles state.json: pending versions switch, removals go, unheld old folders are deleted",
          "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path oldA = rp::host::packs::test::install(root, "packfixture", "1.0.0");
    const fs::path newA = rp::host::packs::test::install(root, "packfixture", "1.1.0");
    const fs::path oldB = rp::host::packs::test::install(root, "packfixture-dependent", "1.0.0");
    const fs::path heldC = rp::host::packs::test::install(root, "packfixture-clone", "1.0.0");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "1.1.0", false});
    state.packs.push_back({"packfixture-dependent", "1.0.0", "", true});
    state.packs.push_back({"packfixture-clone", "1.0.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    // Another process still runs the clone's version.
    auto held = holdVersion(heldC, &why);
    REQUIRE(held);

    REQUIRE(settlePackState(root, &why));
    const auto settled = readPackState(root, &why);
    REQUIRE(settled);
    REQUIRE(settled->packs.size() == 1);
    CHECK(settled->packs[0].id == "packfixture");
    CHECK(settled->packs[0].active == "1.1.0");
    CHECK(settled->packs[0].pending.empty());
    CHECK_FALSE(fs::exists(oldA));
    CHECK(fs::exists(newA));
    CHECK_FALSE(fs::exists(oldB.parent_path()));  // the removed pack's folder, emptied, goes too
    CHECK(fs::exists(heldC));                    // held: left for a later launch

    held.reset();
    REQUIRE(settlePackState(root, &why));
    CHECK_FALSE(fs::exists(heldC));
}

#if !defined(_WIN32)
TEST_CASE("a data root this process cannot write is read without a lock", "[packs-folders]") {
    // A read-only profile or a shared install: no state.lock can be made there, and nothing can write state.json
    // there either, so the read goes ahead unheld rather than failing.
    if (geteuid() == 0) SKIP("root writes through any permission, so a read-only folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    fs::remove(root / kStateLockFile);
    fs::permissions(root, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
    const auto state = readPackState(root, &why);
    fs::permissions(root, fs::perms::owner_write, fs::perm_options::add);  // so the TempDir can go
    INFO(why);
    REQUIRE(state);
    CHECK(state->find("packfixture") != nullptr);
    CHECK_FALSE(fs::exists(root / kStateLockFile));
}
#endif
