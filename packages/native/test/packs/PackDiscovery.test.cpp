// Which packs a launch loads (docs/downloadable-packs-loading-design.md section 1): each installed pack's active
// version, the development folders after them, a development pack standing in for an installed one of its id, and a
// launch that settles state.json and loads all of them in dependency order. Every case runs on a TempDir.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-discovery]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackDiscovery.hpp"
#include "host/packs/PackState.hpp"
#include "host/packs/PackTable.hpp"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::fixture;
using rp::host::packs::test::TempDir;

TEST_CASE("a launch loads each installed pack's active version, and a development pack overrides it",
          "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path installed = rp::host::packs::test::install(root, "packfixture");
    rp::host::packs::test::install(root, "packfixture-dependent");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "", false});
    state.packs.push_back({"packfixture-dependent", "1.0.0", "", false});
    REQUIRE(writePackState(root, state, &why));

    const auto plain = discoverPacks(tmp.roots(), {});
    REQUIRE(plain.candidates.size() == 2);
    CHECK(plain.candidates[0].dir == installed);
    CHECK(plain.candidates[0].origin == PackOrigin::Installed);
    CHECK(plain.refused.empty());

    // The staged build folder of the same id stands in for the installed one, in its place.
    const fs::path dev = rp::host::packs::test::fixture("packfixture");
    const auto overridden = discoverPacks(tmp.roots(), {dev, dev});
    REQUIRE(overridden.candidates.size() == 2);
    CHECK(overridden.candidates[0].dir == dev);
    CHECK(overridden.candidates[0].origin == PackOrigin::Development);
    CHECK(overridden.candidates[0].overridesInstalled);
    // The same id given twice as a development pack: the first loads, the second is refused.
    REQUIRE(overridden.refused.size() == 1);
    CHECK_THAT(overridden.refused[0].reason, ContainsSubstring("is given twice"));

    // A development folder with no readable manifest is refused, and nothing else changes.
    const auto broken = discoverPacks(tmp.roots(), {tmp.path() / "nowhere"});
    CHECK(broken.candidates.size() == 2);
    REQUIRE(broken.refused.size() == 1);
    CHECK_THAT(broken.refused[0].reason, ContainsSubstring("cannot read"));
}

TEST_CASE("a launch settles state.json, then loads the installed and the development packs in dependency order",
          "[packs-discovery]") {
    // The installed fixture and, from RETROPLUG_PACK_PATH, the pack that needs it, given first: the dependent one
    // loads second all the same.
    TempDir tmp;
    const auto installed = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "", false});
    REQUIRE(writePackState(tmp.roots().data, state, &why));
    const rp::host::packs::test::ScopedEnv path("RETROPLUG_PACK_PATH", fixture("packfixture-dependent").string());

    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {fixture("packfixture-noentry")});
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].id == "packfixture-noentry");
    const auto order = table.loadedFrom(0);
    REQUIRE(order.size() == 2);
    CHECK(order[0]->manifest.id == "packfixture");
    CHECK(order[0]->origin == PackOrigin::Installed);
    CHECK(order[0]->dir == installed);
    CHECK(order[1]->manifest.id == "packfixture-dependent");
    CHECK(order[1]->origin == PackOrigin::Development);
}

TEST_CASE("a launch holds the installed versions it loads, so another launch's settle keeps them",
          "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto loaded = rp::host::packs::test::install(root, "packfixture", "1.0.0");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    PackTable table(rp::graph::productPackages());
    REQUIRE(loadLaunchPacks(table, tmp.roots(), {}).empty());
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->lock != nullptr);  // the hold discovery took, handed to the table
    CHECK(versionInUse(loaded));

    // An update installed while this process runs, then another launch's settle: 1.1.0 becomes active, and 1.0.0,
    // which this process has loaded, stays on disk until no process holds it.
    const auto next = rp::host::packs::test::install(root, "packfixture", "1.1.0");
    const auto update = [](PackState& s) {
        s.find("packfixture")->pending = "1.1.0";
        return true;
    };
    REQUIRE(updatePackState(root, update, &why));
    REQUIRE(settlePackState(root, &why));
    CHECK(readPackState(root, &why)->find("packfixture")->active == "1.1.0");
    CHECK(fs::exists(loaded / "pack.json"));
    CHECK(fs::exists(next / "pack.json"));
}

TEST_CASE("a launch that waits on another's settle discovers what the settle left, never what it deleted",
          "[packs-discovery]") {
    // The interleaving a launch can meet, driven in order: another launch holds state.lock (it is settling), and while
    // it holds it, this launch starts and must wait; the other then deletes the version this launch would have found
    // and drops it from state.json, and lets go. This launch discovers only then, so it loads nothing and refuses
    // nothing, rather than finding a folder the settle is deleting.
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto doomed = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));

    auto other = lockPackState(root, FileLock::Mode::Exclusive, &why);
    REQUIRE(other);
    PackTable table(rp::graph::productPackages());
    std::vector<PackRefusal> refused;
    std::atomic<bool> done{false};
    std::thread launch([&] {
        refused = loadLaunchPacks(table, tmp.roots(), {});
        done = true;
    });
    // While the other launch holds the lock, this one cannot have read state.json, whatever the scheduling.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(done.load());
    fs::remove_all(doomed);
    {
        // The other launch's write, under the hold it already has (writePackState would take the lock again).
        std::ofstream(root / "state.json", std::ios::binary) << R"({"version": 1, "packs": []})" << "\n";
    }
    other.reset();
    launch.join();
    CHECK(refused.empty());
    CHECK(table.size() == 0);
}

TEST_CASE("a development folder inside the installed root is held like an installed one", "[packs-discovery]") {
    // A folder offered as a development pack that is in fact packs/<id>/<version>/ of the data root: the launch holds
    // it under state.lock, or another launch's settle could delete a folder this process has a library open from.
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    {
        PackTable table(rp::graph::productPackages());
        REQUIRE(loadLaunchPacks(table, tmp.roots(), {dir}).empty());
        const LoadedPack* pack = table.find("packfixture");
        REQUIRE(pack != nullptr);
        CHECK(pack->origin == PackOrigin::Development);
        CHECK(pack->lock != nullptr);
        CHECK(versionInUse(dir));
    }
    CHECK_FALSE(versionInUse(dir));
    // A development folder outside the root takes no hold: nothing settles it.
    PackTable outside(rp::graph::productPackages());
    REQUIRE(loadLaunchPacks(outside, tmp.roots(), {fixture("packfixture")}).empty());
    CHECK(outside.find("packfixture")->lock == nullptr);
}

#if !defined(_WIN32)
TEST_CASE("a launch from a data root it cannot write loads its installed packs unheld, without settling",
          "[packs-discovery]") {
    if (geteuid() == 0) SKIP("root writes through any permission, so a read-only folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto installed = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    fs::remove(root / kStateLockFile);
    const auto readOnly = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    for (const fs::path& dir : {root, installed}) fs::permissions(dir, readOnly, fs::perm_options::remove);
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {});
    for (const fs::path& dir : {root, installed}) fs::permissions(dir, fs::perms::owner_write, fs::perm_options::add);
    CHECK(refused.empty());
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->lock == nullptr);
}
#endif

TEST_CASE("a launch reports a state.json that will not read once", "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    fs::create_directories(root);
    std::ofstream(root / "state.json", std::ios::binary) << "not json";
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {});
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].id == "state.json");
    CHECK(table.size() == 0);
}
