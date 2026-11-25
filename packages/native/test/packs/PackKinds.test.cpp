// A pack's kinds half (docs/downloadable-packs-m2f-plan.md): its contribution-kind items' runtimes, global code
// compiled and trial-loaded once by the table, beside the built-in script nodes and the earlier packs' halves, and
// refused for a chunk that throws, an id a built-in package or an earlier pack holds, a kind no package declares or ids
// its pack.json does not list; its items in the pack list the TS loader reads.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-kinds]"`.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "BareRuntime.hpp"
#include "PackTestSupport.hpp"
#include "GraphDocs.hpp"
#include "host/js/PackLoaderCore.hpp"
#include "host/packs/KindsTrial.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProtocolTrial.hpp"
#include "host/script/ScriptHost.hpp"
#include "quickjs.h"

using rp::host::packs::PackTable;
using rp::host::packs::test::BareRuntime;
using rp::host::packs::test::fixture;
using rp::host::packs::test::MidiOut;
using rp::host::packs::test::runBlock;

extern "C" {
extern const std::uint8_t  rp_script_test_bundle[];
extern const std::uint32_t rp_script_test_bundle_size;
}

namespace {

// The table with the backend's trials, as the process's has.
void withTrials(PackTable& table) {
    table.setProtocolTrial(rp::host::packs::protocolTrial());
    table.setKindsTrial(rp::host::packs::kindsTrial());
}

}  // namespace

TEST_CASE("a kinds half is compiled and trial-loaded beside the built-in script nodes, its bytecode kept",
          "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("kindfix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->kinds);
    CHECK_FALSE(load.pack->kinds->bytecode.empty());
}

TEST_CASE("a kinds half is refused when it throws, takes a built-in id or names an undeclared kind", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(fixture(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("kindfix-throws", "the kinds fixture throws");
    refused("kindfix-taken", "the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds");
    refused("kindfix-unknown", "a contribution kind this product does not declare");
}

TEST_CASE("a table with no kinds trial refuses a kinds half", "[packs-kinds]") {
    PackTable table;
    const auto load = table.load(fixture("kindfix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack kindfix has a kinds half, and this host loads none");
}

TEST_CASE("the pack list carries the kinds member, and pack:<id>/kinds is no module", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    BareRuntime rt;
    rp::host::installBarePackLoader(rt.ctx, table, nullptr, 0);

    const std::string list = rt.packList();
    INFO(list);
    const std::size_t halves = list.find(R"("halves":["kinds"])");
    REQUIRE(halves != std::string::npos);
    CHECK(list.find(R"("kinds":{"controllerApps":[{"id":"kindfix-app"}]})", halves) != std::string::npos);

    CHECK(rt.importResult("pack:kindfix/kinds").rfind("error could not load 'pack:kindfix/kinds'", 0) == 0);
}

TEST_CASE("a kinds half is held to its pack.json: an id it does not list is refused", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("kindfix-undeclared"));
    CHECK_FALSE(load);
    INFO(load.error);
    CHECK(load.error.find("its kinds half registers controllerApps items kindfix-undeclared-other, and its pack.json's "
                          "kinds member declares kindfix-undeclared-app") != std::string::npos);
    CHECK(table.find("kindfix-undeclared") == nullptr);
}

TEST_CASE("a kinds half that takes an id an earlier pack's holds is refused, the earlier pack staying loaded",
          "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    const auto load = table.load(fixture("kindfix-twin"));
    CHECK_FALSE(load);
    INFO(load.error);
    CHECK(load.error.find("the controllerApps item kindfix-app, which an earlier pack holds") != std::string::npos);
    CHECK(table.find("kindfix-twin") == nullptr);
    CHECK(table.find("kindfix") != nullptr);
}

TEST_CASE("a kinds half that does not compile, or whose file is missing, is refused", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto syntax = table.load(fixture("kindfix-syntax"));
    CHECK_FALSE(syntax);
    INFO(syntax.error);
    CHECK(syntax.error.find("its kinds half does not compile") != std::string::npos);
    const auto missing = table.load(fixture("kindfix-missing"));
    CHECK_FALSE(missing);
    INFO(missing.error);
    CHECK(missing.error.find("halves.kinds names js/gone.js, which the pack does not have") != std::string::npos);
}

namespace {

// The session, out of the host: host MIDI out is what its app's target sends. No surface is wired, and the session
// builds its app on its first block whatever the link says.
std::string sessionDocument(const std::string& app) {
    return R"({"nodes":[{"id":"s","type":"rp.controller.session","settings":{"app":")" + app +
           R"(","target":"midiOut","systemId":1}},{"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
           R"("wires":[{"from":"s.midiOut","to":"out.in1"}]})";
}

}  // namespace

TEST_CASE("an Engine built after the load runs the pack's kind item's runtime", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    Engine e(48000.0, table);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(sessionDocument("kindfix-app"), {})).ok);
    CHECK(runBlock(e, {}) == std::vector<MidiOut>{{1, 0, {0xB0, 0x7F, 0x2A}}});
    CHECK(e.refusedPacks().empty());
}

TEST_CASE("an Engine built before the load refuses the pack by name, and its session finds no app", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    Engine early(48000.0, table);
    REQUIRE(table.load(fixture("kindfix")));
    REQUIRE(early.setGraph(rp::host::test::graphRequest(sessionDocument("kindfix-app"), {})).ok);
    CHECK(early.refusedPacks() == std::vector<std::string>{"kindfix"});
    CHECK(early.refusalReason("kindfix").find("reach an Engine built after it loaded") != std::string::npos);
    CHECK(runBlock(early, {}).empty());
}

TEST_CASE("an Engine on another script bundle loads no kinds half and refuses the pack", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    const rp::host::ScriptHost::Chunk bundle[] = {{rp_script_test_bundle, rp_script_test_bundle_size}};
    Engine e(48000.0, bundle, table);
    CHECK(e.refusedPacks() == std::vector<std::string>{"kindfix"});
    CHECK(e.refusalReason("kindfix").find("run on the product's script bundle, and this engine runs another") !=
          std::string::npos);
}

TEST_CASE("a pack's runtime code finds the kind registrar closed once the Engine is built", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix-late")));
    Engine e(48000.0, table);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(sessionDocument("kindfix-late-app"), {})).ok);
    CHECK(runBlock(e, {}) == std::vector<MidiOut>{{1, 0, {0xB0, 0x7F, 1}}});
}

TEST_CASE("a kinds half that defines script nodes is refused by the table", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("kindfix-nodes"));
    CHECK_FALSE(load);
    INFO(load.error);
    CHECK(load.error.find("its kinds half defines script nodes, which a kinds half may not") != std::string::npos);
    CHECK(table.find("kindfix-nodes") == nullptr);
}

TEST_CASE("a kinds half that fails only in an Engine drops every kinds half there, and the built-ins still run",
          "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix-engine-throws")));
    Engine e(48000.0, table);
    CHECK(e.refusedPacks() == std::vector<std::string>{"kindfix-engine-throws"});
    CHECK(e.refusalReason("kindfix-engine-throws").find("did not load beside the built-in script nodes") !=
          std::string::npos);
    // The built-in session node is still registered, and the pack's app is not found: its host sent nothing.
    REQUIRE(e.setGraph(rp::host::test::graphRequest(sessionDocument("kindfix-engine-throws-app"), {})).ok);
    CHECK(runBlock(e, {}).empty());
}

TEST_CASE("the pack build's kinds half runs in an Engine built after the load", "[packs-kinds]") {
    // tsfix-kind, built from its package by tools/build-pack-halves.mjs: what a real pack's kind items are.
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("tsfix-kind"));
    INFO(load.error);
    REQUIRE(load);
    Engine e(48000.0, table);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(sessionDocument("tsfix-app"), {})).ok);
    CHECK(runBlock(e, {}) == std::vector<MidiOut>{{1, 0, {0xB0, 0x7F, 0x2A}}});
    CHECK(e.refusedPacks().empty());
}
