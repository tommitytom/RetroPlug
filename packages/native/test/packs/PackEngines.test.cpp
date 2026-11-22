// Loaded packs in Engines (docs/downloadable-packs-loading-design.md section 2): an Engine registers the built-ins,
// then every pack its table has loaded, in load order; a pack loaded while an Engine exists joins it at its next
// setGraph, on its control thread; two Engines share one load; an Engine that holds a type already refuses the whole
// pack and only there; and a live load while an audio thread renders (tools/run-sanitizer.sh thread runs this binary).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-engines]"`.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "GraphDocs.hpp"
#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProcessPacks.hpp"

// The fixture's types as this binary links them (retroplug-packfixture-nodes), beside the module's own copy.
extern "C" const rp_package* rp_packfixture_package(void);

using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 256;

// The fixture's tone at `level` on the host's stereo pair.
Engine::GraphRequest toneDocument(float level) {
    const std::string doc = R"({"nodes":[{"id":"tone","type":"rp.packfixture.tone","settings":{"level":)" +
                            std::to_string(level) +
                            R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)"
                            R"("wires":[{"from":"tone.out","to":"out.out.1-2"}]})";
    return rp::host::test::graphRequest(doc, {});
}

// One block's first left sample.
float firstSample(Engine& e) {
    std::vector<float> l(kFrames, -1.0f), r(kFrames, -1.0f);
    e.processBlock(kFrames, l.data(), r.data());
    return l[0];
}

std::string refusal(const Engine::GraphResult& r) {
    return r.errors.empty() ? std::string() : r.errors[0].code + ": " + r.errors[0].message;
}

}  // namespace

TEST_CASE("an Engine registers its table's packs after the built-ins, and a document places their types",
          "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("packfixture")));
    PackTable none(rp::graph::productPackages());
    Engine    bare(kRate, none);
    Engine    e(kRate, table);
    // Two more types than an engine with no pack, and the built-ins as they are.
    CHECK(e.nodeTypeCount() == bare.nodeTypeCount() + 2);
    CHECK(e.packsRegistered() == 1);
    const auto placed = e.setGraph(toneDocument(0.5f));
    INFO(refusal(placed));
    REQUIRE(placed.ok);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("a pack loaded while an Engine runs joins it at its next setGraph", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine e(kRate, table);
    const std::size_t before = e.nodeTypeCount();
    const auto unknown = e.setGraph(toneDocument(0.5f));
    CHECK_FALSE(unknown.ok);
    REQUIRE(table.load(fixture("packfixture")));
    CHECK(e.nodeTypeCount() == before);  // nothing reaches into the engine: it reads the table when it plans
    const auto placed = e.setGraph(toneDocument(0.5f));
    INFO(refusal(placed));
    REQUIRE(placed.ok);
    CHECK(e.nodeTypeCount() == before + 2);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("two Engines share one load, and an Engine built after it sees it at once", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine first(kRate, table);
    Engine second(kRate, table);
    REQUIRE(table.load(fixture("packfixture")));
    REQUIRE(first.setGraph(toneDocument(0.25f)).ok);
    REQUIRE(second.setGraph(toneDocument(0.75f)).ok);
    CHECK(firstSample(first) == 0.25f);
    CHECK(firstSample(second) == 0.75f);
    Engine third(kRate, table);  // a render job's engine, or a second plugin instance
    CHECK(third.packsRegistered() == 1);
    REQUIRE(third.setGraph(toneDocument(0.125f)).ok);
    CHECK(firstSample(third) == 0.125f);
    CHECK(table.size() == 1);  // one library, opened once, for all three
}

TEST_CASE("an Engine that already holds a pack's type refuses the whole pack, and only that Engine does",
          "[packs-engines]") {
    // This binary links the fixture's source statically too: an engine given that copy first (addNodePackage, which
    // the table cannot see) holds rp.packfixture.tone, so the loaded pack is refused there and nowhere else.
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    Engine other(kRate, table);
    REQUIRE(table.load(fixture("packfixture")));
    const std::size_t held = holder.nodeTypeCount();
    REQUIRE(holder.setGraph(toneDocument(0.5f)).ok);  // the static copy's tone
    CHECK(holder.nodeTypeCount() == held);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"packfixture"});
    CHECK(holder.packsRegistered() == 0);
    REQUIRE(other.setGraph(toneDocument(0.5f)).ok);    // the pack's
    CHECK(other.refusedPacks().empty());
    CHECK(firstSample(holder) == 0.5f);
    CHECK(firstSample(other) == 0.5f);
}

TEST_CASE("a pack of two packages whose second clashes in an Engine registers neither there", "[packs-engines]") {
    // The pair's second package takes rp.packfixture.tone and .gain, which the holder has from this binary's static
    // copy: the first package, which clashes with nothing, must stay out with it. The other engine takes all four.
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    Engine other(kRate, table);
    const std::size_t held = holder.nodeTypeCount(), plain = other.nodeTypeCount();
    REQUIRE(table.load(fixture("packfixture-pair")));
    const std::string pairTone = R"({"nodes":[{"id":"tone","type":"rp.packfixture-pair.tone"},)"
                                 R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)"
                                 R"("wires":[{"from":"tone.out","to":"out.out.1-2"}]})";
    CHECK_FALSE(holder.setGraph(rp::host::test::graphRequest(pairTone, {})).ok);
    CHECK(holder.nodeTypeCount() == held);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"packfixture-pair"});
    REQUIRE(other.setGraph(rp::host::test::graphRequest(pairTone, {})).ok);
    CHECK(other.nodeTypeCount() == plain + 4);
    CHECK(firstSample(other) == 0.25f);  // the tone's default level
}

TEST_CASE("a pack requiring one an Engine refused is refused there too, naming the requirement", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    Engine other(kRate, table);
    const auto loads = table.loadAll({{fixture("packfixture")}, {fixture("packfixture-dependent")}});
    REQUIRE(loads.size() == 2);
    REQUIRE(loads[0].pack);
    REQUIRE(loads[1].pack);
    const std::string dependentTone =
        R"({"nodes":[{"id":"tone","type":"rp.packfixture-dependent.tone"},)"
        R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)"
        R"("wires":[{"from":"tone.out","to":"out.out.1-2"}]})";
    CHECK_FALSE(holder.setGraph(rp::host::test::graphRequest(dependentTone, {})).ok);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"packfixture", "packfixture-dependent"});
    CHECK(holder.packsRegistered() == 0);
    REQUIRE(other.setGraph(rp::host::test::graphRequest(dependentTone, {})).ok);
    CHECK(other.refusedPacks().empty());
    CHECK(other.packsRegistered() == 2);
}

TEST_CASE("a script reload refused at its replan leaves the registry sound beside a pack that clashed with the "
          "generation it drops",
          "[packs-engines]") {
    // test.clash's first version holds test.clash.tone, so the loaded scriptclash pack is refused in this Engine. The
    // second drops that type, and the running document places it, so the replan is refused and the swap undone: with
    // loaded packs synced inside that window, the pack would have taken the type the undo needs.
    const auto script = [](bool withTone) {
        return std::string(R"JS(
globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || [];
var ports = [{ id: "out", dir: "out", kind: "events" }];
var layout = { ports: ports, params: [] };
function node(id) {
  return {
    id: id,
    descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
    parseSettings: function (raw) { return raw === undefined ? {} : raw; },
    layout: function () { return layout; },
    create: function () { return { process: function () {} }; },
    reshapes: false,
    faultOnThrow: false,
  };
}
)JS") + (withTone ? "globalThis.__rp_scriptNodes.push(node('test.clash.tone'));\n"
                  : "globalThis.__rp_scriptNodes.push(node('test.clash.other'));\n");
    };
    PackTable table(rp::graph::productPackages());
    Engine e(kRate, table);
    const auto first = e.loadScriptPack("test.clash", script(true));
    INFO(first.error);
    REQUIRE(first.ok);
    const std::string placeTone = R"({"nodes":[{"id":"t","type":"test.clash.tone"}],"wires":[]})";
    REQUIRE(e.setGraph(rp::host::test::graphRequest(placeTone, {})).ok);
    REQUIRE(table.load(fixture("packfixture-scriptclash")));
    const auto reload = e.loadScriptPack("test.clash", script(false));
    INFO(reload.error);
    CHECK_FALSE(reload.ok);
    CHECK(e.refusedPacks() == std::vector<std::string>{"packfixture-scriptclash"});
    // The old generation is still the registry's: the same document plans again.
    CHECK(e.setGraph(rp::host::test::graphRequest(placeTone, {})).ok);
}

TEST_CASE("an Engine built with no table of its own reads the process's", "[packs-engines]") {
    // The one case that loads into the process's table, which outlives it: every later default-built engine in this
    // binary sees the fixture, and no other case builds one.
    rp::host::packs::PackTable& process = rp::host::packs::processPacks();
    CHECK(&process == &rp::host::packs::processPacks());
    REQUIRE(process.load(fixture("packfixture")));
    Engine e(kRate);
    CHECK(e.packsRegistered() == 1);
    REQUIRE(e.setGraph(toneDocument(0.5f)).ok);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("a pack loads on another thread while an audio thread renders, and the next plan runs its node",
          "[packs-engines][threads]") {
    // Three threads, as a plugin has them: the audio thread running blocks, a loader (an install from the UI) and the
    // control thread that plans. The table's lock orders the load against the control thread's read, and the
    // registry changes on the control thread between plans, which the audio thread never reads.
    PackTable table(rp::graph::productPackages());
    Engine e(kRate, table);
    e.setAudioThreadOwns(true);
    std::atomic<bool>  running{true};
    std::atomic<int>   toned{0};
    std::thread        audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.processBlock(kFrames, l.data(), r.data());
            if (l[0] == 0.5f) toned.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    std::thread loader([&] { table.load(fixture("packfixture")); });
    bool        placed = false;
    std::string why;
    const auto  patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!placed && std::chrono::steady_clock::now() < patience) {
        const auto r = e.setGraph(toneDocument(0.5f));
        placed = r.ok;
        if (!placed) {
            why = refusal(r);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        e.reclaim();
    }
    loader.join();
    while (placed && toned.load() < 10 && std::chrono::steady_clock::now() < patience)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    INFO(why);
    REQUIRE(placed);
    CHECK(toned.load() >= 10);
}
