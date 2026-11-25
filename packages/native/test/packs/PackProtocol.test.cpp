// A pack's protocol half (docs/downloadable-packs-m2e-plan.md): its global-code node bundle compiled and trial-loaded
// once by the table, beside the built-in script nodes, and refused for a type it does not declare, one it declares and
// does not define, a clash, a bundle that does not compile or throws; its data in the pack list the TS loader reads.
// Every Engine's own load of it (Engine::syncPacks) is tested with the Engine.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-protocol]"`.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "GraphDocs.hpp"
#include "BareRuntime.hpp"
#include "PackTestSupport.hpp"
#include "host/js/PackLoaderCore.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProtocolTrial.hpp"
#include "host/script/ScriptHost.hpp"
#include "quickjs.h"

namespace fs = std::filesystem;
using rp::host::packs::PackTable;
using rp::host::packs::test::BareRuntime;
using rp::host::packs::test::fixture;
using rp::host::packs::test::MidiOut;
using rp::host::packs::test::runBlock;
using rp::host::packs::test::TempDir;

namespace {

// The table with the backend's trial, as the process's has.
void withTrial(PackTable& table) { table.setProtocolTrial(rp::host::packs::protocolTrial()); }

}  // namespace

TEST_CASE("a protocol half is compiled and trial-loaded once, its types and descriptors kept", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    const auto load = table.load(fixture("protofix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->protocol);
    const auto& half = *load.pack->protocol;
    CHECK(half.scriptId == "rp.protofix.protocol");
    CHECK_FALSE(half.bytecode.empty());
    CHECK(half.typeIds == std::vector<std::string>{"rp.protofix.up", "rp.protofix.idle"});
    REQUIRE(half.descriptors.size() == 2);
    CHECK(half.descriptors[0].find(R"("id":"rp.protofix.up")") != std::string::npos);
    CHECK(half.descriptors[0].find(R"("id":"midiOut")") != std::string::npos);
    CHECK(load.pack->packages.empty());  // no native half and no script half
}

TEST_CASE("a protocol half is refused for what it defines, what it misses, a clash, a compile and a throw",
          "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(fixture(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("protofix-undeclared", "pack protofix-undeclared's protocol half defines node type "
                                   "rp.protofix-undeclared.extra, which its pack.json's protocol.nodes does not declare");
    refused("protofix-missing", "pack protofix-missing's pack.json's protocol.nodes declares rp.protofix-missing.gone, "
                                "which its protocol half does not define");
    refused("protofix-clash", "rp.midi.route");
    refused("protofix-syntax", "pack protofix-syntax: its protocol half does not compile");
    refused("protofix-throws", "the protocol fixture throws");
}

TEST_CASE("a table with no protocol trial refuses a protocol half", "[packs-protocol]") {
    PackTable table;
    const auto load = table.load(fixture("protofix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack protofix has a protocol half, and this host loads none");
}

TEST_CASE("a pack whose type an earlier pack's protocol half holds is refused", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));

    // A second pack naming protofix's type under another id, with protofix's bundle copied in.
    TempDir tmp;
    const fs::path dir = tmp.path() / "protofix-again";
    fs::create_directories(dir / "js");
    fs::copy_file(fixture("protofix") / "js" / "protocol.js", dir / "js" / "protocol.js");
    std::ofstream(dir / "pack.json")
        << "{\"id\": \"protofix-again\", \"name\": \"n\", \"version\": \"1.0.0\", \"apiLevel\": " << RP_API_LEVEL_CMAKE
        << ", \"license\": \"MIT\", \"source\": \"s\", \"requires\": {\"packs\": [], \"platforms\": []}, "
           "\"provides\": {\"platforms\": [], \"nodeTypes\": [\"rp.protofix.up\", \"rp.protofix.idle\"], \"roles\": []}, "
           "\"halves\": {\"protocol\": \"js/protocol.js\"}, \"protocol\": {\"nodes\": [{\"type\": \"rp.protofix.up\", "
           "\"midiIn\": \"in\", \"writes\": [], \"midiOut\": true}, {\"type\": \"rp.protofix.idle\"}], \"roles\": {}}}";
    const auto load = table.load(dir);
    CHECK_FALSE(load);
    CHECK(load.error == "pack protofix-again registers node type rp.protofix.up, which package rp.protofix.protocol has "
                        "registered already");
    CHECK(table.find("protofix-again") == nullptr);
}

TEST_CASE("the pack list carries a protocol half's data, and pack:<id>/protocol is no module", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));
    BareRuntime rt;
    rp::host::installBarePackLoader(rt.ctx, table, nullptr, 0);

    const std::string list = rt.packList();
    INFO(list);
    const std::size_t halves = list.find(R"("halves":["protocol"])");
    REQUIRE(halves != std::string::npos);
    const std::string expected =
        R"("protocol":{"nodes":[{"midiIn":"in","midiOut":true,"type":"rp.protofix.up","writes":[]},{"type":"rp.protofix.idle"}],"roles":{},"descriptors":{"rp.protofix.up":{)";
    CHECK(list.find(expected, halves) != std::string::npos);

    CHECK(rt.importResult("pack:protofix/protocol").rfind("error could not load 'pack:protofix/protocol'", 0) == 0);
}

TEST_CASE("an app pack is loaded once and never reloaded", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    const auto load = table.load(fixture("protofix"));
    REQUIRE(load);
    rp::host::ScriptHost host;
    const rp::host::ScriptHost::Chunk chunk{load.pack->protocol->bytecode};
    std::string error;
    const rp_package* first = host.loadAppPack("rp.protofix.protocol", {&chunk, 1}, 0, &error);
    INFO(error);
    REQUIRE(first != nullptr);
    CHECK(host.loadAppPack("rp.protofix.protocol", {&chunk, 1}, 0, &error) == nullptr);
    CHECK(error == "the app pack rp.protofix.protocol is loaded already: an app pack is never reloaded");
    CHECK(host.loadAppPack("protofix", {&chunk, 1}, 0, &error) == nullptr);
    host.discardPack(first);
}

namespace {

// Host MIDI in -> rp.protofix.up -> host MIDI out, under system 1.
const char* kUpDocument =
    R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"up","type":"rp.protofix.up"},)"
    R"({"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
    R"("wires":[{"from":"in.midi","to":"up.in"},{"from":"up.midiOut","to":"out.in1"}]})";

}  // namespace

TEST_CASE("an Engine registers a pack's protocol nodes, and its node runs", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));
    Engine e(48000.0, table);
    const auto r = e.setGraph(rp::host::test::graphRequest(kUpDocument, {}));
    INFO((r.errors.empty() ? std::string() : r.errors.front().message));
    REQUIRE(r.ok);
    CHECK(runBlock(e, {{0x90, 60, 100}}) == std::vector<MidiOut>{{1, 0, {0x90, 72, 100}}});
    // Its pack is the engine's app pack, beside the built-in one.
    bool listed = false;
    for (const auto& p : e.scriptPacks()) listed = listed || p.id == "rp.protofix.protocol";
    CHECK(listed);
}

TEST_CASE("two Engines each load it, and one built before the load takes it at its next setGraph", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    Engine early(48000.0, table);
    REQUIRE(table.load(fixture("protofix")));
    Engine late(48000.0, table);
    for (Engine* e : {&early, &late}) {
        REQUIRE(e->setGraph(rp::host::test::graphRequest(kUpDocument, {})).ok);
        CHECK(runBlock(*e, {{0x90, 64, 1}}) == std::vector<MidiOut>{{1, 0, {0x90, 76, 1}}});
    }
}

TEST_CASE("a pack's protocol nodes take the span labels after the built-ins'", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));
    Engine e(48000.0, table);
    const auto names = e.dspTraceNames();
    if (names.empty()) SKIP("this build names no span labels (RETROPLUG_PROFILE is off)");
    const auto at = [&](const std::string& type) {
        return static_cast<std::size_t>(std::find(names.begin(), names.end(), type) - names.begin());
    };
    REQUIRE(at("rp.protofix.up") < names.size());
    CHECK(at("rp.protofix.idle") == at("rp.protofix.up") + 1);
    // Every built-in script node's label is below the pack's.
    CHECK(at("rp.midi.route") < at("rp.protofix.up"));
    CHECK(at("rp.lsdj.serial-out") < at("rp.protofix.up"));
}

TEST_CASE("no caller of loadScriptPack can take a pack's protocol id", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));
    Engine e(48000.0, table);
    const auto r = e.loadScriptPack("rp.protofix.protocol", "var x = 1;");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("is the app's to give") != std::string::npos);
}

TEST_CASE("a protocol half refused in one Engine is freed there and still runs in another", "[packs-protocol]") {
    // A scratch table gives the bundle's bytecode; a script host that outlives the Engines loads it under another id,
    // so its package holds rp.protofix.up, and the holder, given that package first, refuses the pack's own.
    PackTable scratch;
    withTrial(scratch);
    const auto scratchLoad = scratch.load(fixture("protofix"));
    REQUIRE(scratchLoad);
    rp::host::ScriptHost host;
    const rp::host::ScriptHost::Chunk builtIn[] = {rp::host::ScriptHost::embeddedBundle()};
    REQUIRE(host.load(builtIn));
    const rp::host::ScriptHost::Chunk chunk{scratchLoad.pack->protocol->bytecode};
    std::string error;
    const rp_package* held = host.loadAppPack("rp.holder.protocol", {&chunk, 1}, 0, &error);
    INFO(error);
    REQUIRE(held != nullptr);

    PackTable table;
    withTrial(table);
    Engine holder(48000.0, table);
    Engine other(48000.0, table);
    REQUIRE(holder.addNodePackage(held));
    REQUIRE(table.load(fixture("protofix")));
    // The document still places: with the holder's own copy of the node, not the pack's.
    CHECK(holder.setGraph(rp::host::test::graphRequest(kUpDocument, {})).ok);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"protofix"});
    CHECK(holder.packsRegistered() == 0);
    for (const auto& p : holder.scriptPacks()) CHECK(p.id != "rp.protofix.protocol");
    REQUIRE(other.setGraph(rp::host::test::graphRequest(kUpDocument, {})).ok);
    CHECK(other.refusedPacks().empty());
    CHECK(runBlock(other, {{0x90, 60, 100}}) == std::vector<MidiOut>{{1, 0, {0x90, 72, 100}}});
}

TEST_CASE("a protocol half loads while an audio thread renders, and the next plan runs its node",
          "[packs-protocol][threads]") {
    PackTable table;
    withTrial(table);
    Engine e(48000.0, table);
    e.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::thread       audio([&] {
        std::vector<float> l(256), r(256);
        while (running.load()) {
            e.drainCommands();
            e.processBlock(256, l.data(), r.data());
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    std::thread loader([&] { table.load(fixture("protofix")); });
    bool        placed = false;
    const auto  patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!placed && std::chrono::steady_clock::now() < patience) {
        placed = e.setGraph(rp::host::test::graphRequest(kUpDocument, {})).ok;
        if (!placed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        e.reclaim();
    }
    loader.join();
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(placed);
}

TEST_CASE("the pack build's protocol half runs in an Engine", "[packs-protocol]") {
    // tsfix-proto, built from its package by tools/build-pack-halves.mjs: what a real pack's protocol half is.
    PackTable table;
    withTrial(table);
    const auto load = table.load(fixture("tsfix-proto"));
    INFO(load.error);
    REQUIRE(load);
    Engine e(48000.0, table);
    const char* doc =
        R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"echo","type":"rp.tsfix-proto.echo"},)"
        R"({"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
        R"("wires":[{"from":"in.midi","to":"echo.in"},{"from":"echo.midiOut","to":"out.in1"}]})";
    const auto r = e.setGraph(rp::host::test::graphRequest(doc, {}));
    INFO((r.errors.empty() ? std::string() : r.errors.front().message));
    REQUIRE(r.ok);
    CHECK(runBlock(e, {{0x90, 60, 100}}) == std::vector<MidiOut>{{1, 0, {0x90, 67, 100}}});
}
