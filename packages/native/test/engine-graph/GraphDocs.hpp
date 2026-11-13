#pragma once

// Graph documents for native tests, spelled the way packages/retroplug/src/graph/buildGraph.ts writes their audio
// (node ids, document order, buses), so a test drives the Engine with what TS would send. Only the shapes the
// tests need: systems summed to a stereo pair (with a test's own nodes and wires added: host I/O nodes, script nodes,
// the cart node), each system on its own pair, and a lone NES split. None holds the script nodes and the host I/O
// buildGraph places beside the audio: a test that stages MIDI or reads MIDI out adds the nodes it needs.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "SyntheticRom.hpp"  // rp::blastem::test's images (packages/blastem/native/test)
#include "host/engine/Engine.hpp"
#include "system/SystemTypes.hpp"

namespace rp::host::test {

inline std::vector<std::uint8_t> readFile(const char* path) {
    std::vector<std::uint8_t> bytes;
    if (std::FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        bytes.resize(static_cast<std::size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
        std::fclose(f);
    }
    return bytes;
}

// A system a test document places (docs/modular-graph-phase-c3-design.md section 3): its id, the node type whose core
// it builds, the `rom` resource that core is built from, and any further settings or params its case adds.
struct DocSystem {
    SystemId      id;
    const char*   type;  // rp.sameboy.gb / rp.mesen.nes / ...
    std::uint32_t rom = 0;    // the id Engine::putResource gave its ROM (withRoms); 0 names none
    std::string   extra;      // further settings members, JSON, without braces: a knob or a construct-time field
    std::string   params;     // the node's param values, JSON members without braces
};

inline const char* const kGbType = "rp.sameboy.gb";
inline const char* const kNesType = "rp.mesen.nes";

// The ROM bytes a system node of `type` boots in a native test: the same fixture every case used, small enough to put
// per Engine. A GBA node gates the bytes it is given (its package's RomFormat.hpp), so its 1 KiB ROM - the smallest
// GbaConsole::LoadRom takes - carries the Nintendo logo the way every licensed cart does. Empty for a type with no
// fixture.
inline std::vector<std::uint8_t> romFor(const std::string& type) {
    if (type == kGbType) return readFile(RP_MGB_ROM_PATH);
    if (type == kNesType) return readFile(RP_BLIPTOASTER_ROM_PATH);
    if (type == "rp.mesen.sms") return readFile(RP_SMS_ROM_PATH);
    // The Mega Drive's image is generated rather than read: the synthetic cart that every blastem case pins
    // (packages/blastem/native/test/SyntheticRom.hpp), the one whose PSG sounds from power-on.
    if (type == "rp.blastem.md") return rp::blastem::test::fmPsgRom();
    if (type == "rp.mesen.gba") {
        static constexpr std::uint8_t kGbaLogo[] = {
            0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
            0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21, 0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
        };
        std::vector<std::uint8_t> rom(1024, 0);
        std::copy(std::begin(kGbaLogo), std::end(kGbaLogo), rom.begin() + 0x04);
        return rom;
    }
    return {};
}

// The `rom` resource a node of `type` builds from: its fixture put in `engine` now. 0, with the reason in *why, for a
// type with no fixture or a put the engine refused.
inline std::uint32_t putRom(Engine& engine, const std::string& type, std::string* why = nullptr) {
    const auto rom = romFor(type);
    if (rom.empty()) {
        if (why) *why = "no ROM fixture for " + type;
        return 0;
    }
    const std::uint32_t id = engine.putResource("rom", rom, why);
    if (id == 0 && why && why->empty()) *why = "the engine would not take " + type + "'s ROM";
    return id;
}

// The `rom` resource each console's fixture was put under, one per Engine: a case that publishes more than one document
// keeps one of these beside its Engine. A republish that names the same ids leaves every system node's rebuild key
// alone, so the nodes are reused; a fresh put per document moves every ref, and a moved ref is a rebuild, which under an
// id a live core still holds is refused ("system N is live").
struct Roms {
    std::map<std::string, std::uint32_t> ids;
};

// `systems`, each naming the `rom` resource its node type boots: one put per console in `roms`, so two systems of one
// console share the bytes and a later document names what this one did. A system that already names one (a test that put
// its own, a wrong-console ROM among them) keeps it. Throws with the reason when a fixture is missing or refused, as
// NodeRig::put does.
inline void withRoms(Engine& engine, std::vector<DocSystem>& systems, Roms& roms) {
    for (DocSystem& s : systems) {
        if (s.rom != 0) continue;
        const auto cached = roms.ids.find(s.type);
        if (cached != roms.ids.end()) {
            s.rom = cached->second;
            continue;
        }
        std::string why;
        const std::uint32_t rom = putRom(engine, s.type, &why);
        if (rom == 0) throw std::runtime_error(why);
        roms.ids.emplace(s.type, rom);
        s.rom = rom;
    }
}

// `document` with every `"res:<id>"` replaced by the id `ids` gives it, in ONE pass: two ids can each be the other's
// engine id, and replacing them one after another would let the second see the first's output. A document built by the
// helpers below already names the engine's own ids; this is for documents spelled before an Engine had them (the
// goldens' models). Nothing with the reason in *why for a reference that runs off the document, one that is not a
// decimal id, and an id `ids` does not have.
inline std::optional<std::string> rebindResources(const std::string& document,
                                                  const std::map<std::uint32_t, std::uint32_t>& ids,
                                                  std::string* why = nullptr) {
    const auto refuse = [why](std::string reason) {
        if (why) *why = std::move(reason);
        return std::optional<std::string>();
    };
    std::string out;
    for (std::size_t at = 0; at < document.size();) {
        const auto found = document.find("\"res:", at);
        if (found == std::string::npos) {
            out.append(document, at, std::string::npos);
            break;
        }
        out.append(document, at, found - at);
        const auto digits = found + 5;  // past `"res:`
        const auto end = document.find('"', digits);
        if (end == std::string::npos) return refuse("a resource reference runs off the document");
        std::uint32_t wanted = 0;
        const auto parsed = std::from_chars(document.data() + digits, document.data() + end, wanted);
        if (parsed.ec != std::errc() || parsed.ptr != document.data() + end)
            return refuse(document.substr(found, end - found + 1) + " is not a resource id");
        const auto mapped = ids.find(wanted);
        if (mapped == ids.end())
            return refuse("the document names " + document.substr(found, end - found + 1) +
                          ", which no system of the model has");
        out += "\"res:" + std::to_string(mapped->second) + "\"";
        at = end + 1;
    }
    return out;
}

namespace docs_detail {

inline std::string node(const std::string& id, const std::string& type, const std::string& settings = "",
                        const std::string& params = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) +
           (params.empty() ? "" : R"(,"params":{)" + params + "}") + "}";
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
// A system node's settings: the system its core is built for and the `rom` resource it is built from, then whatever
// its case adds (docs/modular-graph-phase-c3-design.md section 3).
inline std::string settings(const DocSystem& s) {
    std::string text = R"({"system":)" + std::to_string(s.id);
    if (s.rom != 0) text += R"(,"rom":"res:)" + std::to_string(s.rom) + "\"";
    if (!s.extra.empty()) text += "," + s.extra;
    return text + "}";
}
inline std::string sys(const DocSystem& s) {
    return node("sys-" + std::to_string(s.id), s.type, settings(s), s.params);
}

}  // namespace docs_detail

// One node, or one wire, as a document spells it, for DocExtras.
inline std::string docNode(const std::string& id, const std::string& type, const std::string& settings = "") {
    return docs_detail::node(id, type, settings);
}
inline std::string docWire(const std::string& from, const std::string& to) { return docs_detail::wire(from, to); }

// What a test adds to a document: nodes after the systems (the host I/O nodes, script nodes, the cart node) and wires
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
inline std::string nesSplitDocument(const DocSystem& nes, const std::string& split) {
    using namespace docs_detail;
    const std::string n = "sys-" + std::to_string(nes.id);
    std::vector<std::string> nodes{sys(nes)}, wires;
    std::vector<std::string> streams =
        split == "channels"
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
