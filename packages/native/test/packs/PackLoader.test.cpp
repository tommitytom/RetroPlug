// The native loader (docs/downloadable-packs-loading-design.md section 2): the fixture pack loads from its staged
// folder, packs load in dependency order, and each broken variant (test/packs/fixture/CMakeLists.txt) is refused with
// its own reason and registers nothing. Each case loads into a PackTable of its own, seeded as the process's is with
// this binary's built-in packages, so no load outlives its case.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-loader]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "ElfSymbols.hpp"
#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "graph/rp_pack.h"
#include "host/packs/FileLock.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackTable.hpp"

// The fixture's types as this binary links them (retroplug-packfixture-nodes), beside the module's own copy.
extern "C" const rp_package* rp_packfixture_package(void);

using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::fixture;
using rp::host::packs::test::TempDir;

namespace {

std::vector<std::string> typesOf(const LoadedPack& pack) {
    std::vector<std::string> ids;
    for (const rp_package* p : pack.packages)
        for (std::uint32_t k = 0; k < p->node_type_count; ++k) ids.emplace_back(p->node_types[k]->id);
    return ids;
}

// `id` refused with a reason holding `reason`, recorded, and nothing added to the table.
void checkRefused(PackTable& table, const std::string& id, const std::string& reason) {
    INFO("pack " << id);
    const std::size_t before = table.size();
    const PackLoad load = table.load(fixture(id));
    INFO(load.error);
    CHECK_FALSE(load);
    CHECK_THAT(load.error, ContainsSubstring(reason));
    CHECK(table.size() == before);
    CHECK(table.find(id) == nullptr);
    REQUIRE_FALSE(table.refusals().empty());
    CHECK(table.refusals().back().id == id);
    CHECK(table.refusals().back().reason == load.error);
}

}  // namespace

TEST_CASE("the fixture pack loads from its staged folder, once", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(fixture("packfixture"));
    INFO(load.error);
    REQUIRE(load);
    CHECK(load.pack->manifest.id == "packfixture");
    CHECK(load.pack->origin == PackOrigin::Development);
    CHECK(load.pack->library != nullptr);
    CHECK(typesOf(*load.pack) == std::vector<std::string>{"rp.packfixture.tone", "rp.packfixture.gain"});
    CHECK(load.pack->packageIds == std::vector<std::string>{"rp.packfixture"});
    CHECK(table.size() == 1);
    CHECK(table.find("packfixture") == load.pack);

    // The same folder again is the same load; the library is opened once.
    const PackLoad again = table.load(fixture("packfixture"));
    CHECK(again.pack == load.pack);
    CHECK(table.size() == 1);

    // Its library exports the entry and nothing else: the package entry it links is its own, and hidden.
    CHECK(librarySymbol(load.pack->library, RP_PACK_ENTRY_SYMBOL) != nullptr);
    CHECK(librarySymbol(load.pack->library, "rp_packfixture_package") == nullptr);
    // And so its package is not this binary's static copy of the same source.
    CHECK(load.pack->packages[0] != rp_packfixture_package());
}

#if defined(__linux__)
TEST_CASE("a pack module exports its entry alone and imports nothing of RetroPlug's", "[packs-loader]") {
    // What the version script and --no-undefined promise (cmake/packs.cmake), read from the files themselves.
    const std::string so = "native/" + std::string(currentPlatform()) + "/";
    const auto good =
        rp::host::packs::test::readDynamicSymbols((fixture("packfixture") / so / "rp-packfixture.so").string());
    REQUIRE(good);
    CHECK(good->exported == std::vector<std::string>{"rp_pack_entry"});
    for (const auto& name : good->imported) {
        INFO(name);
        CHECK_FALSE(rp::host::packs::test::isRetroPlugSymbol(name));
    }
    const auto withheld = rp::host::packs::test::readDynamicSymbols(
        (fixture("packfixture-noentry") / so / "rp-packfixture-noentry.so").string());
    REQUIRE(withheld);
    CHECK(withheld->exported == std::vector<std::string>{"rp_pack_entry_withheld"});
}
#endif

TEST_CASE("packs load in dependency order, whatever order they are given in", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const auto loads = table.loadAll({{fixture("packfixture-dependent")}, {fixture("packfixture")}});
    REQUIRE(loads.size() == 2);
    REQUIRE(loads[0]);
    REQUIRE(loads[1]);
    CHECK(loads[0].pack->manifest.id == "packfixture");
    CHECK(loads[1].pack->manifest.id == "packfixture-dependent");
    const auto order = table.loadedFrom(0);
    REQUIRE(order.size() == 2);
    CHECK(order[0]->manifest.id == "packfixture");
    CHECK(table.loadedFrom(1).size() == 1);
}

TEST_CASE("a pack whose requirement is not loaded is refused, naming it", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-dependent", "requires pack packfixture, which is not loaded");
    // Through loadAll too: it waits for a pack that never comes, then is refused.
    const auto loads = table.loadAll({{fixture("packfixture-dependent")}});
    REQUIRE(loads.size() == 1);
    CHECK_FALSE(loads[0]);
    CHECK(table.size() == 0);
}

TEST_CASE("a pack built for another API level is refused, saying which side is out of date", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-api-newer",
                 "is built for API level " + std::to_string(RP_API_LEVEL + 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update RetroPlug");
    checkRefused(table, "packfixture-api-older",
                 "is built for API level " + std::to_string(RP_API_LEVEL - 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update the pack");
    // A manifest that says this level over a library that reports another: the entry's answer is read first.
    checkRefused(table, "packfixture-api-mismatch",
                 "'s library reports API level " + std::to_string(RP_API_LEVEL + 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update RetroPlug");
}

TEST_CASE("a pack whose library is missing or exports no entry is refused", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-noplatform",
                 std::string("has no library for ") + std::string(currentPlatform()) + " (native/" +
                     std::string(currentPlatform()) + "/rp-packfixture-noplatform.");
    checkRefused(table, "packfixture-noentry", "'s library exports no rp_pack_entry");
    // An entry that throws is refused, the exception caught on this side of the C entry.
    checkRefused(table, "packfixture-throws", "'s rp_pack_entry threw");
    // A TS half beside the native one: M1 loads none, and a pack never loads in part.
    checkRefused(table, "packfixture-tshalf",
                 "has a TS half, halves.app (js/app.js), which this RetroPlug does not load yet");
}

TEST_CASE("a pack that registers what it does not declare, or what is registered already, is refused",
          "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-undeclared",
                 "registers node type rp.packfixture-undeclared.extra, which its pack.json's provides.nodeTypes does "
                 "not declare");
    checkRefused(table, "packfixture-duplicate",
                 "registers node type rp.core.add, which package rp.core has registered already");
    // provides stays truthful the other way too: a declared type the library never registers.
    checkRefused(table, "packfixture-overdeclared",
                 "'s pack.json declares node type rp.packfixture-overdeclared.missing, which its library does not "
                 "register");
    // Against another pack: the clone registers the good pack's types under another package id.
    REQUIRE(table.load(fixture("packfixture")));
    checkRefused(table, "packfixture-clone",
                 "registers node type rp.packfixture.tone, which package rp.packfixture has registered already");
    CHECK(table.size() == 1);
}

TEST_CASE("an installed pack holds its version folder while it is loaded", "[packs-loader]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    {
        PackTable table(rp::graph::productPackages());
        const PackLoad load = table.load(PackCandidate{dir, PackOrigin::Installed, false, nullptr});
        INFO(load.error);
        REQUIRE(load);
        CHECK(load.pack->lock != nullptr);
        CHECK(versionInUse(dir));
    }
    CHECK_FALSE(versionInUse(dir));

    // A refused installed pack holds its folder too: its library may be open, and a pack's library is never closed.
    const auto refusedDir = rp::host::packs::test::install(tmp.roots().data, "packfixture-noentry");
    {
        PackTable table(rp::graph::productPackages());
        CHECK_FALSE(table.load(PackCandidate{refusedDir, PackOrigin::Installed, false, nullptr}));
        CHECK(versionInUse(refusedDir));
        // Retried, it is refused again and its folder still held (one hold per folder, kept by the table).
        CHECK_FALSE(table.load(PackCandidate{refusedDir, PackOrigin::Installed, false, nullptr}));
        CHECK(versionInUse(refusedDir));
    }
    CHECK_FALSE(versionInUse(refusedDir));
    // One refused before its library opens keeps no hold: nothing of its folder is mapped.
    const auto levelDir = rp::host::packs::test::install(tmp.roots().data, "packfixture-api-newer");
    {
        PackTable table(rp::graph::productPackages());
        CHECK_FALSE(table.load(PackCandidate{levelDir, PackOrigin::Installed, false, nullptr}));
        CHECK_FALSE(versionInUse(levelDir));
    }

    // An installed pack must sit in packs/<id>/<version>/: anywhere else, state.json does not describe it.
    const auto stray = rp::host::packs::test::install(tmp.roots().data, "packfixture", "9.9.9");
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(PackCandidate{stray, PackOrigin::Installed, false, nullptr});
    CHECK_FALSE(load);
    CHECK_THAT(load.error, ContainsSubstring("which is not packs/packfixture/1.0.0"));
}

TEST_CASE("a library of two packages loads both, and a type its second takes from a loaded pack refuses it whole",
          "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad pair = table.load(fixture("packfixture-pair"));
    INFO(pair.error);
    REQUIRE(pair);
    CHECK(pair.pack->packageIds == std::vector<std::string>{"rp.packfixture-pair", "rp.packfixture-pair-second"});
    CHECK(typesOf(*pair.pack) == std::vector<std::string>{"rp.packfixture-pair.tone", "rp.packfixture-pair.gain",
                                                           "rp.packfixture.tone", "rp.packfixture.gain"});
    // Its second package holds the good pack's types now, so the good pack is refused, and nothing of it registers.
    checkRefused(table, "packfixture",
                 "registers node type rp.packfixture.tone, which package rp.packfixture-pair-second has registered "
                 "already");
}

TEST_CASE("a second folder of a loaded pack's id is refused until the next launch", "[packs-loader]") {
    TempDir tmp;
    const auto copy = rp::host::packs::test::install(tmp.path(), "packfixture");
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("packfixture")));
    const PackLoad second = table.load(copy);
    CHECK_FALSE(second);
    CHECK_THAT(second.error, ContainsSubstring("is loaded already from"));
    CHECK(table.size() == 1);
}
