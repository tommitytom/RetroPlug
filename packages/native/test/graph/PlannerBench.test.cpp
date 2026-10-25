// How setGraph scales (docs/modular-graph-phase-b2-design.md section 7.4): documents of about 1,000 and
// 10,000 nodes, as a chain (each node feeding the next) and as a wide fan (every node live at once, summed by
// a tree of adds), timed end to end (parse, plan, publish) for a cold plan, where every instance is new, and
// for a warm edit that changes one node and reuses the rest. It reports and does not gate; the numbers are
// recorded in the main design's section 12.
//
// Hidden from the normal run. Run it on its own: build/bin/retroplug-graph-test "[bench]"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphRig.hpp"

using rp::graph::test::GraphRig;

namespace {

std::string delay(std::size_t k, std::uint32_t maxSamples = 1) {
    return R"({"id":"d)" + std::to_string(k) + R"(","type":"rp.core.delay","settings":{"channels":1,"maxSamples":)" +
           std::to_string(maxSamples) + "}}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}
std::string document(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string s = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) s += (i ? "," : "") + nodes[i];
    s += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) s += (i ? "," : "") + wires[i];
    return s + "]}";
}
const std::string kOut = R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}})";

// n delays, each feeding the next; `edited` gives the last one another ring, so it alone rebuilds.
std::string chain(std::size_t n, bool edited) {
    std::vector<std::string> nodes, wires;
    for (std::size_t k = 0; k < n; ++k) nodes.push_back(delay(k, edited && k + 1 == n ? 2 : 1));
    for (std::size_t k = 1; k < n; ++k) wires.push_back(wire("d" + std::to_string(k - 1) + ".out", "d" + std::to_string(k) + ".in"));
    nodes.push_back(kOut);
    wires.push_back(wire("d" + std::to_string(n - 1) + ".out", "out.out.1"));
    return document(nodes, wires);
}

// n delays side by side, summed 16 at a time by a tree of adds into the host's one channel.
std::string fan(std::size_t n, bool edited) {
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
            nodes.push_back(R"({"id":")" + id + R"(","type":"rp.core.add","settings":{"inputs":)" + std::to_string(width) + "}}");
            for (std::size_t i = 0; i < width; ++i) wires.push_back(wire(level[at + i], id + ".in" + std::to_string(i + 1)));
            next.push_back(id + ".out");
        }
        level = std::move(next);
    }
    nodes.push_back(kOut);
    wires.push_back(wire(level[0], "out.out.1"));
    return document(nodes, wires);
}

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST_CASE("setGraph scales with the document", "[.][bench]") {
    std::printf("\n%-6s %7s %9s %12s %12s\n", "shape", "nodes", "bytes", "cold ms", "edit ms");
    for (const char* shape : {"chain", "fan"}) {
        for (std::size_t n : {1000u, 10000u}) {
            const std::string cold = shape[0] == 'c' ? chain(n, false) : fan(n, false);
            const std::string edit = shape[0] == 'c' ? chain(n, true) : fan(n, true);
            GraphRig rig({48000.0, 1024, rp::graph::Target::Desktop}, 1);
            auto t0 = std::chrono::steady_clock::now();
            REQUIRE(rig.set(cold).ok);
            const double coldMs = msSince(t0);
            rig.run(1024);  // the plan runs; reclaim nothing yet
            t0 = std::chrono::steady_clock::now();
            REQUIRE(rig.set(edit).ok);
            const double editMs = msSince(t0);
            std::printf("%-6s %7zu %9zu %12.2f %12.2f\n", shape, rig.host->instanceCount(), edit.size(), coldMs, editMs);
        }
    }
}
