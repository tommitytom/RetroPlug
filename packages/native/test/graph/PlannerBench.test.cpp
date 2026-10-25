// How native setGraph scales (docs/modular-graph-phase-b2-design.md section 7.4): documents of about 1,000 and
// 10,000 nodes, as a chain (each node feeding the next) and as a wide fan (every node live at once, summed by
// a tree of adds). Each row times parseGraphDocument and GraphHost::setGraph(document) apart, for a cold plan,
// where every instance is new, and for an edit that gives one delay another ring, so it alone rebuilds and the
// rest are reused. The edit checks that reuse: one that silently rebuilt everything would time much like a cold
// plan. The host is quiescent, so setGraph also adopts the new plan and frees the one it replaced, inline. Not
// timed: TS buildGraph and JSON.stringify, the RPC hop, and Engine::setGraph's per-system checks. Each figure
// is a median: cold of kColdRuns fresh rigs, edit of kEdits edits alternating between two documents, each an
// edit of the other. It reports and does not gate; the numbers are recorded in the main design's section 12.
//
// Hidden from the normal run. Run it on its own: build/bin/retroplug-graph-test "[bench]"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphRig.hpp"
#include "graph/GraphDocument.hpp"

using rp::graph::test::GraphRig;

namespace {

constexpr int kColdRuns = 3;
constexpr int kEdits = 5;

// Release and RelWithDebInfo both define NDEBUG, and nothing in the source tells them apart.
#ifdef NDEBUG
constexpr const char* kBuild = "NDEBUG (release or RelWithDebInfo)";
#else
constexpr const char* kBuild = "debug (numbers not comparable)";
#endif

// A document's text and what it holds. Every node in it is one instance once planned.
struct Doc {
    std::string text;
    std::size_t nodes = 0;
    std::size_t wires = 0;
};

std::string delay(std::size_t k, std::uint32_t maxSamples = 1) {
    return R"({"id":"d)" + std::to_string(k) + R"(","type":"rp.core.delay","settings":{"channels":1,"maxSamples":)" +
           std::to_string(maxSamples) + "}}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}
Doc document(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    Doc d{R"({"nodes":[)", nodes.size(), wires.size()};
    for (std::size_t i = 0; i < nodes.size(); ++i) d.text += (i ? "," : "") + nodes[i];
    d.text += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) d.text += (i ? "," : "") + wires[i];
    d.text += "]}";
    return d;
}
const std::string kOut = R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}})";

// n delays, each feeding the next; `edited` gives the last one another ring, so it alone rebuilds.
Doc chain(std::size_t n, bool edited) {
    std::vector<std::string> nodes, wires;
    for (std::size_t k = 0; k < n; ++k) nodes.push_back(delay(k, edited && k + 1 == n ? 2 : 1));
    for (std::size_t k = 1; k < n; ++k)
        wires.push_back(wire("d" + std::to_string(k - 1) + ".out", "d" + std::to_string(k) + ".in"));
    nodes.push_back(kOut);
    wires.push_back(wire("d" + std::to_string(n - 1) + ".out", "out.out.1"));
    return document(nodes, wires);
}

// n delays side by side, summed 16 at a time by a tree of adds into the host's one channel; `edited` as chain.
Doc fan(std::size_t n, bool edited) {
    std::vector<std::string> nodes, wires;
    std::vector<std::string> level;
    for (std::size_t k = 0; k < n; ++k) {
        nodes.push_back(delay(k, edited && k + 1 == n ? 2 : 1));
        level.push_back("d" + std::to_string(k) + ".out");
    }
    std::size_t adds = 0;
    while (level.size() > 1) {
        std::vector<std::string> next;
        for (std::size_t at = 0; at < level.size(); at += 16) {
            const std::size_t width = std::min<std::size_t>(16, level.size() - at);
            const std::string id = "a" + std::to_string(adds++);
            nodes.push_back(R"({"id":")" + id + R"(","type":"rp.core.add","settings":{"inputs":)" +
                            std::to_string(width) + "}}");
            for (std::size_t i = 0; i < width; ++i)
                wires.push_back(wire(level[at + i], id + ".in" + std::to_string(i + 1)));
            next.push_back(id + ".out");
        }
        level = std::move(next);
    }
    nodes.push_back(kOut);
    wires.push_back(wire(level[0], "out.out.1"));
    return document(nodes, wires);
}

struct Shape {
    const char* name;
    Doc (*build)(std::size_t n, bool edited);
};
const Shape kShapes[] = {{"chain", chain}, {"fan", fan}};

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// One parse and one setGraph of the parsed document, timed apart.
struct Timing {
    std::vector<double> parse, plan;
    void time(GraphRig& rig, const std::string& text) {
        auto t0 = Clock::now();
        const auto doc = rp::graph::parseGraphDocument(text);
        parse.push_back(msSince(t0));
        REQUIRE(doc);
        t0 = Clock::now();
        const bool ok = rig.host->setGraph(*doc).ok;
        plan.push_back(msSince(t0));
        REQUIRE(ok);
    }
    void print() const {
        const double p = median(parse), q = median(plan);
        std::printf(" | %8.2f %8.2f %8.2f", p, q, p + q);
    }
};

}  // namespace

TEST_CASE("setGraph scales with the document", "[.][bench]") {
    const auto start = Clock::now();
    std::printf("\nnative setGraph in a quiescent host, %s build\n", kBuild);
    std::printf("medians: cold of %d fresh rigs, edit of %d edits alternating between two documents\n", kColdRuns,
                kEdits);
    std::printf("plan = setGraph(parsed document): plan, publish, adopt, and free any plan it replaced\n");
    std::printf("%-29s | %-26s | %s\n", "", "cold ms", "edit ms");
    std::printf("%-6s %6s %6s %8s | %8s %8s %8s | %8s %8s %8s\n", "shape", "nodes", "wires", "bytes", "parse", "plan",
                "sum", "parse", "plan", "sum");
    for (const Shape& shape : kShapes) {
        for (std::size_t n : {1000u, 10000u}) {
            const Doc docs[2] = {shape.build(n, false), shape.build(n, true)};
            const std::string last = "d" + std::to_string(n - 1);  // the delay the edit rebuilds
            Timing cold, edit;
            std::unique_ptr<GraphRig> rig;
            for (int run = 0; run < kColdRuns; ++run) {
                rig = std::make_unique<GraphRig>(rp::graph::HostConfig{48000.0, 1024, rp::graph::Target::Desktop}, 1);
                cold.time(*rig, docs[0].text);
                REQUIRE(rig->host->instanceCount() == docs[0].nodes);
            }
            for (int at = 0; at < kEdits; ++at) {
                rig->run(1024);  // a block, so the edit replaces a plan that has run
                rp_node* const first = rig->host->node("d0");
                rp_node* const before = rig->host->node(last);
                REQUIRE((first && before));
                edit.time(*rig, docs[(at + 1) % 2].text);
                rp_node* const after = rig->host->node(last);
                REQUIRE(rig->host->node("d0") == first);  // reused
                REQUIRE((after && after != before));       // rebuilt
                REQUIRE(rig->host->instanceCount() == docs[0].nodes);
            }
            std::printf("%-6s %6zu %6zu %8zu", shape.name, docs[0].nodes, docs[0].wires, docs[0].text.size());
            cold.print();
            edit.print();
            std::printf("\n");
        }
    }
    std::printf("total %.2f s\n", msSince(start) / 1000.0);
}
