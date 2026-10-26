#pragma once

// Graph documents for native tests, spelled the way packages/retroplug/src/graph/buildGraph.ts writes their audio
// (node ids, document order, buses), so a test drives the Engine with what TS would send. Only the shapes the
// tests need: systems summed to a stereo pair (with a test's own nodes and wires added: host I/O nodes, a kernel, a
// cart mirror), each system on its own pair, and a lone NES split. None holds the kernel and the host I/O buildGraph
// places beside the audio: a test that stages MIDI or reads MIDI out adds the nodes it needs.

#include <cstdint>
#include <string>
#include <vector>

#include "host/engine/Engine.hpp"
#include "system/SystemTypes.hpp"

namespace rp::host::test {

struct DocSystem {
    SystemId    id;
    const char* type;  // rp.sameboy.gb / rp.mesen.nes / ...
};

inline const char* const kGbType = "rp.sameboy.gb";
inline const char* const kNesType = "rp.mesen.nes";

namespace docs_detail {

inline std::string node(const std::string& id, const std::string& type, const std::string& settings = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
}
inline std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}
inline std::string join(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) s += (i ? "," : "") + items[i];
    return s;
}
inline std::string out(std::uint32_t first, std::uint32_t n) {
    return "out.out." + std::to_string(first + 1) + (n == 1 ? "" : "-" + std::to_string(first + n));
}
inline std::string buses(const std::vector<std::uint32_t>& b) {
    std::string s = R"({"buses":[)";
    for (std::size_t i = 0; i < b.size(); ++i) s += (i ? "," : "") + std::to_string(b[i]);
    return s + "]}";
}
inline std::string document(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    return R"({"nodes":[)" + join(nodes) + R"(],"wires":[)" + join(wires) + "]}";
}
inline std::string sys(const DocSystem& s) {
    return node("sys-" + std::to_string(s.id), s.type, R"({"core":)" + std::to_string(s.id) + "}");
}

}  // namespace docs_detail

// One node, or one wire, as a document spells it, for DocExtras.
inline std::string docNode(const std::string& id, const std::string& type, const std::string& settings = "") {
    return docs_detail::node(id, type, settings);
}
inline std::string docWire(const std::string& from, const std::string& to) { return docs_detail::wire(from, to); }

// What a test adds to a document: nodes after the systems (the host I/O nodes, a kernel, a cart mirror) and wires
// after the routing's.
struct DocExtras {
    std::vector<std::string> nodes;
    std::vector<std::string> wires;
};

// The Stereo routing at a `channels`-wide host: every system's L summed into channel 0, its R into 1. One
// rp.core.add per side, never chained as buildGraph chains them past 16 inputs, so 16 systems at most.
inline std::string stereoDocument(const std::vector<DocSystem>& systems, const DocExtras& extras, std::uint32_t channels = 2) {
    using namespace docs_detail;
    std::vector<std::string> nodes, wires;
    for (const DocSystem& s : systems) nodes.push_back(sys(s));
    for (const DocSystem& s : systems) {
        const std::string split = "split-" + std::to_string(s.id);
        nodes.push_back(node(split, "rp.core.split"));
        wires.push_back(wire("sys-" + std::to_string(s.id) + ".mix", split + ".in"));
    }
    for (std::uint32_t c = 0; c < 2 && !systems.empty(); ++c) {
        const std::string half = c ? ".out2" : ".out1";
        if (systems.size() == 1) {
            wires.push_back(wire("split-" + std::to_string(systems[0].id) + half, out(c, 1)));
            continue;
        }
        const std::string lane = "lane-" + std::to_string(c);
        nodes.push_back(node(lane, "rp.core.add", R"({"inputs":)" + std::to_string(systems.size()) + "}"));
        for (std::size_t k = 0; k < systems.size(); ++k)
            wires.push_back(wire("split-" + std::to_string(systems[k].id) + half, lane + ".in" + std::to_string(k + 1)));
        wires.push_back(wire(lane + ".out", out(c, 1)));
    }
    nodes.insert(nodes.end(), extras.nodes.begin(), extras.nodes.end());
    wires.insert(wires.end(), extras.wires.begin(), extras.wires.end());
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(channels, 1))));
    return document(nodes, wires);
}
inline std::string stereoDocument(const std::vector<DocSystem>& systems, std::uint32_t channels = 2) {
    return stereoDocument(systems, DocExtras{}, channels);
}

// The perSystem shape: slot i's mix on host channels 2i and 2i + 1.
inline std::string perSystemDocument(const std::vector<DocSystem>& systems) {
    using namespace docs_detail;
    std::vector<std::string> nodes, wires;
    for (std::size_t i = 0; i < systems.size(); ++i) {
        nodes.push_back(sys(systems[i]));
        wires.push_back(wire("sys-" + std::to_string(systems[i].id) + ".mix", out(static_cast<std::uint32_t>(2 * i), 2)));
    }
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(systems.size(), 2))));
    return document(nodes, wires);
}

// A lone NES split across an 8-channel host: its pins one per channel ("pins"), each pin on its own pair
// ("stereoPins"), or its five channels one per channel ("channels").
inline std::string nesSplitDocument(SystemId id, const std::string& split) {
    using namespace docs_detail;
    const std::string n = "sys-" + std::to_string(id);
    std::vector<std::string> nodes{sys({id, kNesType})}, wires;
    std::vector<std::string> streams = split == "channels"
                                           ? std::vector<std::string>{"channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"}
                                           : std::vector<std::string>{"pins.pulse", "pins.tnd", "pins.expansion"};
    const std::uint32_t stride = split == "stereoPins" ? 2 : 1;
    for (std::uint32_t k = 0; k < streams.size(); ++k)
        for (std::uint32_t l = 0; l < stride; ++l) wires.push_back(wire(n + "." + streams[k], out(stride * k + l, 1)));
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(8, 1))));
    return document(nodes, wires);
}

// A request for `document` placing `slots`, with no tap re-arm.
inline Engine::GraphRequest graphRequest(std::string document, std::vector<SystemId> slots) {
    Engine::GraphRequest r;
    r.document = std::move(document);
    r.slots = std::move(slots);
    return r;
}
// `r`, re-arming the NES tap: restore, then arm `armId` to `armMode`, or restore only when `armMode` is 0. The
// arm is read off the mode, so this cannot ask for an arm to mode 0, which the engine refuses
// (EngineGraph.test.cpp's own arming() can).
inline Engine::GraphRequest rearming(Engine::GraphRequest r, SystemId armId = 0, std::uint32_t armMode = 0) {
    r.rearmTap = true;
    r.arm = armMode != 0;
    r.armId = armId;
    r.armMode = armMode;
    return r;
}

}  // namespace rp::host::test
