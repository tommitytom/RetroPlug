// The protocol script nodes (docs/modular-graph-phase-b3b-design.md section 2): the MIDI routing, a controller session
// and each system's protocol nodes, as buildGraph places them, of the production script bundle in a plan, fed by the
// host I/O nodes and a stand-in for the Game Boys' serialOut, what reaches every system input read back by probes,
// block by block.
//
// Each structure's document is buildGraph's own: the case of its name among buildGraph's role goldens
// (packages/retroplug/test/graph/golden/build-graph-roles.json, its "rig/" cases, recorded from and held to buildGraph
// by build-graph-role-goldens.test.ts), with its systems stood in for and its audio and cart mirror left out
// (Structure, below). So the graph these cases hold to their values is the one buildGraph builds for them: a change to
// its wiring for these structures fails that TS test until it is re-recorded, and then fails a case here if it moves
// what reaches a system input or the host. It does not reach what the stand-ins replace: the system nodes, the audio
// and the cart mirror (BuildGraphGoldens.test.cpp plans these same documents whole on real cores), nor the feedback
// wire a Game Boy's MI.OUT comes back on, whose feed here is a plain wire carrying what the core captured last block.
// Nor any model but these, or what the stores hand buildGraph. The plan order is buildGraph's: the feeds lead the
// document, so the script nodes run in their document order, as production plans them, and a case holds every
// structure to that. The throw case relies on it: only a node planned after the one that throws can show that a throw
// stops no other.
//
// A case that ends in rig.matches is held to the values the DSP kernel those nodes replaced was recorded sending: the
// messages of each kind, and a digest of all of them (per block, per system and input, each message's frame, flags and
// bytes in order; the host MIDI out with its system tags in emission order; the control-surface out). A digest hashes
// what reaches each system input and the host outputs, never which node sent it, so a split of the kernel's work over
// nodes is held to what the kernel sent. That is B3b's proof, bit for bit, that splitting the kernel changed nothing a
// core or the host can see in those cases, and in no others. The design's section 9 lists where the per-role graph
// behaves otherwise than the kernel did: among them a node's state keyed by its role's kind, so a role removed and
// added again on a surviving system starts afresh; the controller's default target following a change of first system;
// its session rebuilt on a change to its parsed config rather than its raw one; its launches going only into an
// lsdj-sync node; a config read with its defaults filled. No case here sends a message through any of those; the throw
// is the one difference a case reaches, below.
//
// The values have a history. The kernel node (B3a) was run beside the native kernel stage it replaced and found equal
// to it block by block, from c323c7a8 to e3de2e46, the last commit before the stage's deletion; the B3a prototype
// recorded the values from the same cases, and they held as recorded. One was re-recorded since: the LSDj modes case,
// when master sync's stop became a silence rather than a flood (docs/lsdj-master-sync-fix-design.md). One case came
// after the stage had gone, recorded from the kernel node alone: the controller's launches behind routed MIDI and its
// host MIDI out ahead of the systems', two orders no earlier case sends a message through, which the stage-era proof
// therefore never reached. Then, until B3b deleted the kernel node (from the commit that held its recorded digests on
// the per-role graph too, to the flip that had buildGraph place that graph), every case ran on both, the kernel node's
// graph and this one, each held to the same values. So a changed value is a change to what reaches a core or the host:
// a deliberate one is re-recorded from the values a failing case prints. The other cases, which came after the kernel
// or record nothing, check what they hold directly: a block that fills rp.host.midi-in, a controller's session kept
// through a system added behind it, every structure's script nodes running in buildGraph's order, and the restructuring
// under an audio thread.
//
// A serial input is counted and hashed byte by byte, each byte as the one-byte message (its frame, flags 0, one byte)
// the kernel node sent when the values were recorded: a node sends a run of same-frame bytes as one message, which the
// system node applies byte by byte at its frame, so where a run breaks, which no core can see, moves no value. Every
// other input is hashed message for message.
//
// Where a case reaches a difference from the kernel by design (the throw: it stops only the node it is in, design
// section 5), the case says so, checks what the per-role graph does instead, and the digest takes the one input that
// differs as the kernel had it.
//
// Last, the graph runs on an audio thread while the control thread restructures it, for the thread sanitizer.
//
// Run via `pnpm test:plugin engine-graph`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rfl/json.hpp>

#include "NodeRig.hpp"
#include "ScriptRig.hpp"
#include "TestNodes.hpp"
#include "graph/CanonicalJson.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/Queue.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/script/ScriptHost.hpp"

using rp::host::ScriptHost;
using rp::host::test::Feed;
using rp::host::test::Message;
using rp::host::test::Probe;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double kRate = 44100.0;
constexpr std::uint32_t kBeat = 22050;  // frames per quarter at 120 bpm
// ---- structures, as buildGraph places them --------------------------------------------------------------------

// buildGraph's role goldens (packages/retroplug/test/graph/golden/build-graph-roles.json): each document it builds for
// a model, recorded from build-graph-role-goldens.test.ts, which holds the file to buildGraph. Its "rig/" cases are
// this file's structures.
struct DocNode {
    std::string                 id;
    std::string                 type;
    std::optional<rfl::Generic> settings;
};
struct DocWire {
    std::string         from;
    std::string         to;
    std::optional<bool> feedback;
};
struct Document {
    std::vector<DocNode> nodes;
    std::vector<DocWire> wires;
};
struct GoldenCase {
    std::string                name;
    std::size_t                document = 0;
    std::vector<std::uint32_t> slots;
};
struct RoleGoldens {
    std::vector<Document>   documents;
    std::vector<GoldenCase> cases;
};

// Through parseJson, each part re-spelled and read typed (the models, which the rig does not need, ignored).
RoleGoldens readRoleGoldens() {
    const auto bytes = rp::host::test::readFile(RP_BUILD_GRAPH_ROLE_GOLDENS_PATH);
    REQUIRE_FALSE(bytes.empty());
    const auto root = rp::graph::parseJson(std::string(bytes.begin(), bytes.end()));
    REQUIRE(root);
    const auto object = root->to_object();
    REQUIRE(object);
    const auto documents = object->get("documents").and_then([](const rfl::Generic& g) { return g.to_array(); });
    const auto cases = object->get("cases");
    REQUIRE(documents);
    REQUIRE(cases);
    RoleGoldens out;
    for (const rfl::Generic& d : *documents) {
        auto doc = rfl::json::read<Document>(rp::graph::canonicalJson(d));
        REQUIRE(doc);
        out.documents.push_back(std::move(*doc));
    }
    auto parsed = rfl::json::read<std::vector<GoldenCase>>(rp::graph::canonicalJson(*cases));
    REQUIRE(parsed);
    out.cases = std::move(*parsed);
    return out;
}

std::string nodeJson(const std::string& id, const std::string& type, const std::string& settings = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
}
std::string wireJson(const std::string& from, const std::string& to, bool feedback = false) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"" + (feedback ? R"(,"feedback":true)" : "") + "}";
}
std::string documentJson(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string doc = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) doc += (i ? "," : "") + nodes[i];
    doc += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) doc += (i ? "," : "") + wires[i];
    return doc + "]}";
}

// The probe input a system input stands in for: serialIn and midi on one probe, bytes and buttons on another.
std::string probeFor(std::uint32_t system, const std::string& input) {
    const std::string id = std::to_string(system);
    if (input == "serialIn") return "in-" + id + ".bytes";
    if (input == "midi") return "in-" + id + ".events";
    if (input == "bytes") return "io-" + id + ".bytes";
    INFO(input);
    REQUIRE(input == "buttons");
    return "io-" + id + ".events";
}

// `endpoint` (node.port) split at its first dot: a node id holds none, a port may hold more (out.out.1-2).
std::pair<std::string, std::string> splitEndpoint(const std::string& endpoint) {
    const auto dot = endpoint.find('.');
    REQUIRE(dot != std::string::npos);
    return {endpoint.substr(0, dot), endpoint.substr(dot + 1)};
}

// A structure the rig plans: one of buildGraph's role goldens, its document as buildGraph built it but for what the
// rig stands in for. Each system node is replaced where it stood by a probe for its serialIn and midi inputs (in-<id>)
// and one for its bytes and buttons (io-<id>). A Game Boy's serialOut is stood in for by a feed (mi-out-<id>), which
// the rig fills with what the core captured last block: so the wire from it into an LSDj's node is a plain one, where
// buildGraph's from the Game Boy is a feedback wire, the feed's bytes being a block late already. The feeds lead the
// document, which keeps buildGraph's plan order: a feedback wire orders nothing, and a plain one from a feed where its
// system stood would hold the LSDj node back until the feed, behind every script node placed after it (the planner
// takes the first ready node in document order), while a feed at the head, with no inputs, is planned before them all.
// The audio nodes and the N8 cart mirror are left out (no core runs here; HostIo.test.cpp holds the mirror), with every
// wire to or from them or a system node that no stand-in takes. Every other node and wire, the script nodes' with their
// settings and the host I/O's, is buildGraph's as recorded.
struct PlacedNode {
    std::string id;
    std::string type;
    std::string settings;  // canonical JSON, empty for none
};
struct Structure {
    std::string                name;
    std::vector<std::uint32_t> systems;   // in slot order
    std::vector<std::uint32_t> gameBoys;  // the systems a serialOut feed stands in for
    std::vector<PlacedNode>    nodes;     // each node of buildGraph's kept, in its document order
    std::string                document;  // what the rig plans
};

const std::set<std::string> kSystemTypes{"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"};
const std::set<std::string> kLeftOut{"rp.core.split", "rp.core.add", "rp.host.audio-out", "rp.n8.cart-mirror"};

Structure standIns(const GoldenCase& c, const Document& doc) {
    Structure s{c.name, c.slots, {}, {}, {}};
    std::map<std::string, std::uint32_t> systemOf;  // a system node's id, and its system's
    for (const std::uint32_t id : s.systems) systemOf["sys-" + std::to_string(id)] = id;
    std::set<std::string>    leftOut;
    std::vector<std::string> feeds, nodes, wires;
    for (const DocNode& n : doc.nodes) {
        if (kSystemTypes.count(n.type)) {
            INFO(n.id);
            REQUIRE(systemOf.count(n.id) == 1);
            const std::uint32_t system = systemOf.at(n.id);
            const std::string   id = std::to_string(system);
            if (n.type == "rp.sameboy.gb") {
                s.gameBoys.push_back(system);
                feeds.push_back(nodeJson("mi-out-" + id, "rp.test.feed"));
            }
            nodes.push_back(nodeJson("in-" + id, "rp.test.probe"));
            nodes.push_back(nodeJson("io-" + id, "rp.test.probe"));
            continue;
        }
        if (kLeftOut.count(n.type)) {
            leftOut.insert(n.id);
            continue;
        }
        const std::string settings = n.settings ? rp::graph::canonicalJson(*n.settings) : "";
        nodes.push_back(nodeJson(n.id, n.type, settings));
        s.nodes.push_back({n.id, n.type, settings});
    }
    for (const DocWire& w : doc.wires) {
        const auto [fromNode, fromPort] = splitEndpoint(w.from);
        const auto [toNode, toPort] = splitEndpoint(w.to);
        if (leftOut.count(fromNode) || leftOut.count(toNode)) {
            // A node the rig leaves out may feed only another it leaves out or a system, whose stand-in takes no wire
            // from it: one feeding a script node it plans would drop an input from the case without a word.
            INFO(w.from << " -> " << w.to);
            REQUIRE((!leftOut.count(fromNode) || leftOut.count(toNode) || systemOf.count(toNode) == 1));
            continue;
        }
        const auto fromSystem = systemOf.find(fromNode);
        const auto toSystem = systemOf.find(toNode);
        if (fromSystem != systemOf.end()) {
            // A Game Boy's MI.OUT into its LSDj's node comes from its feed. Past the audio, left out above, the one
            // other wire a system sends is a link cable into another system, which no stand-in takes: any other would
            // be a system output buildGraph wires to a node the rig plans, which must fail here rather than vanish.
            if (fromPort == "serialOut" && toSystem == systemOf.end()) {
                wires.push_back(wireJson("mi-out-" + std::to_string(fromSystem->second) + ".bytes", w.to));
                continue;
            }
            INFO(w.from << " -> " << w.to);
            REQUIRE(toSystem != systemOf.end());
            continue;
        }
        wires.push_back(wireJson(w.from, toSystem != systemOf.end() ? probeFor(toSystem->second, toPort) : w.to,
                                 w.feedback.value_or(false)));
    }
    feeds.insert(feeds.end(), nodes.begin(), nodes.end());
    s.document = documentJson(feeds, wires);
    return s;
}

const RoleGoldens& roleGoldens() {
    static const RoleGoldens goldens = readRoleGoldens();
    return goldens;
}

// The rig's structures, by name: the role goldens named "rig/<name>".
std::vector<std::string> rigStructureNames() {
    std::vector<std::string> names;
    for (const GoldenCase& c : roleGoldens().cases)
        if (c.name.rfind("rig/", 0) == 0) names.push_back(c.name.substr(4));
    return names;
}

// The rig's structure `name`: buildGraph's role golden "rig/<name>", with its stand-ins.
Structure structure(const std::string& name) {
    const RoleGoldens& goldens = roleGoldens();
    const auto it = std::find_if(goldens.cases.begin(), goldens.cases.end(),
                                 [&](const GoldenCase& c) { return c.name == "rig/" + name; });
    INFO("rig/" << name << ": a case of build-graph-role-goldens.test.ts, recorded into build-graph-roles.json");
    REQUIRE(it != goldens.cases.end());
    REQUIRE(it->document < goldens.documents.size());
    return standIns(*it, goldens.documents[it->document]);
}

// ---- one block's input -------------------------------------------------------------------------------------------

// What one Game Boy captured last block, as its serialOut stamps it: each byte at the frame its core completed it, here
// the first at `first` and each next `stride` frames on (MI.OUT's port clocks a byte every eight frames; a playing LSDj
// master clocks one every few hundred).
struct Captured {
    std::uint32_t system;
    Bytes         bytes;
    std::uint32_t stride = 8;
    std::uint32_t first = 7;
};

struct BlockIn {
    std::uint32_t                                     frames = 1024;
    double                                            ppq = 0.0;
    double                                            tempo = 120.0;
    bool                                              playing = false;
    std::vector<std::pair<std::uint32_t, Bytes>>      midi;        // as a host stages it, in any frame order
    std::vector<Bytes>                                controller;  // control-surface traffic, at frame 0
    bool                                              connected = false;
    std::vector<Captured>                             serialOut;   // what each Game Boy captured last block
};

Bytes on(int channel, int key, int velocity = 100) {
    return {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
}
Bytes off(int channel, int key) { return {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(key), 0}; }
Bytes cc(int channel, int number, int value) {
    return {static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)};
}

// What a case's graph sent, over every block: the messages of each kind, and their digest.
struct Sent {
    int           serialIn = 0, midi = 0, bytes = 0, buttons = 0, midiOut = 0, controllerOut = 0;
    std::uint64_t digest = 0;
    bool operator==(const Sent&) const = default;
};

// FNV-1a, 64 bits, over every field the graph sent, lengths included.
struct Digest {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    void byte(std::uint8_t b) { h = (h ^ b) * 0x100000001b3ULL; }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) byte(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void data(const Bytes& d) {
        u32(static_cast<std::uint32_t>(d.size()));
        for (const std::uint8_t b : d) byte(b);
    }
    void messages(const std::vector<Message>& ms) {
        u32(static_cast<std::uint32_t>(ms.size()));
        for (const Message& m : ms) {
            u32(m.frame);
            u32(m.flags);
            data(m.data);
        }
    }
};

// As the recorded values are written, so a failing case prints what to re-record.
std::string text(const Sent& s) {
    char b[160];
    std::snprintf(b, sizeof b, "{%d, %d, %d, %d, %d, %d, 0x%016llxULL}", s.serialIn, s.midi, s.bytes, s.buttons, s.midiOut,
                  s.controllerOut, static_cast<unsigned long long>(s.digest));
    return b;
}

// A message list as text, so a failed comparison shows both sides.
std::string text(const std::vector<Message>& ms) {
    std::string s;
    char b[16];
    for (const Message& m : ms) {
        s += std::to_string(m.frame) + ":";
        for (const std::uint8_t x : m.data) {
            std::snprintf(b, sizeof b, "%02x", x);
            s += b;
        }
        if (m.flags) s += "/" + std::to_string(m.flags);
        s += " ";
    }
    return s;
}

// A serial input's messages as the system node applies them: each byte at its message's frame, in order.
std::vector<Message> serialBytes(const std::vector<Message>& ms) {
    std::vector<Message> out;
    for (const Message& m : ms)
        for (const std::uint8_t b : m.data) out.push_back({m.frame, {b}, m.flags});
    return out;
}

// ---- the rig ---------------------------------------------------------------------------------------------------

struct ProtocolRig {
    // The script node every document places, which every structure change keeps.
    static constexpr const char* keptId = "routing";
    Structure structure;
    // What a script calls __probe with (ScriptRig.hpp's probes, bound into both runtimes): the order case's record.
    rp::host::test::ProbeLog          log;
    // Before the graph, so it is destroyed after it: a script node's shim hands its instance back to it.
    ScriptHost                        scripts{&rp::host::test::bindProbes, &log};
    rp::graph::NodeRegistry           registry;
    rp::graph::ResourceTable          resources;
    // The engine's host I/O, standing in: what is staged for a block, and what the output nodes hand back.
    std::vector<std::pair<std::uint32_t, Bytes>> stagedMidi;
    std::vector<Bytes>                           stagedController;
    bool                                         connected = false;
    struct Out {
        std::uint32_t system;
        std::uint32_t frame;
        Bytes         data;
    };
    std::vector<Out>                  midiOut;
    std::vector<Bytes>                controllerOut;
    rp::host::EngineHostIo            hostIo{this, &takeMidi, &takeController, &isConnected, &recordMidiOut, &recordControllerOut,
                                             &noCart};
    std::unique_ptr<rp::graph::GraphHost> host;
    rp_node*                          kept = nullptr;  // keptId's instance, as the first plan made it
    int                               blocks = 0;
    Sent                              sent;
    Digest                            digest;
    // System inputs (a system and an input's name) whose messages the next block's digest takes as the kernel sent
    // them, none, where the per-role graph sends some by design: a case sets it for one block, and checks what the
    // per-role graph sent instead.
    std::vector<std::pair<std::uint32_t, std::string>> unsentByKernel;

    // The node types the script bundle registers: a document's script nodes are its nodes of these.
    std::set<std::string>             scriptTypes;

    // `inject`: JS run after the bundle, in both of the script host's runtimes.
    explicit ProtocolRig(Structure s, const std::string& inject = "") : structure(std::move(s)) {
        std::vector<ScriptHost::Chunk> chunks{ScriptHost::embeddedBundle()};
        Bytes injected;
        if (!inject.empty()) {
            const auto bc = dsp::compileToBytecode(inject);
            REQUIRE(bc.has_value());
            injected = *bc;
            chunks.emplace_back(injected);
        }
        std::string error;
        const bool loaded = scripts.load(chunks, &error);
        INFO(error);
        REQUIRE(loaded);
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::hostIoPackage(),
                                    rp::host::test::testPackage(), scripts.package()})
            REQUIRE(registry.addPackage(p, &error));
        for (std::uint32_t i = 0; i < scripts.package()->node_type_count; ++i)
            scriptTypes.insert(scripts.package()->node_types[i]->id);
        rp::graph::HostConfig config;
        config.sampleRate = kRate;
        config.maxFrames = 8192;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            return std::string(id) == RP_ENGINE_HOST_IO_INTERFACE ? &static_cast<ProtocolRig*>(ctx)->hostIo : nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
        plan();
        kept = host->node(keptId);
        REQUIRE(kept != nullptr);
    }
    ~ProtocolRig() { host.reset(); }

    // A structure change, as the stores publish one: each node kept through a swap or a reshape. Every script node the
    // next structure places that is placed now keeps its instance; and once the replaced plan is reclaimed, the
    // instances live are the nodes placed, one each, so a node placed anew (a system added, or removed and added again)
    // is a new instance and one no longer placed is gone.
    void restructure(Structure next) {
        std::map<std::string, rp_node*> placed;
        for (const std::string& id : scriptNodes(next)) placed[id] = host->node(id);  // null for one placed anew
        structure = std::move(next);
        plan();
        host->reclaim();
        scripts.reclaim();
        for (const auto& [id, before] : placed) {
            INFO(id);
            REQUIRE(host->node(id) != nullptr);
            if (before) CHECK(host->node(id) == before);
        }
        CHECK(scripts.liveInstances() == placed.size());
    }

    // The script nodes a structure places, in document order: the routing, the controller when the project has one,
    // then each system's protocol nodes in slot order, each in its pipeline's order.
    std::vector<std::string> scriptNodes(const Structure& s) const {
        std::vector<std::string> ids;
        for (const PlacedNode& n : s.nodes)
            if (scriptTypes.count(n.type)) ids.push_back(n.id);
        return ids;
    }

    void plan() {
        INFO(structure.name);
        const auto r = host->setGraph(document());
        for (const auto& e : r.errors) UNSCOPED_INFO(e.code << " at " << e.nodeId << "." << e.port << ": " << e.message);
        REQUIRE(r.ok);
    }

    // The per-role graph, as buildGraph places it (docs/modular-graph-phase-b3b-design.md sections 2, 4 and 5), with
    // its stand-ins: what Structure says.
    const std::string& document() const { return structure.document; }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        REQUIRE(t != nullptr);
        return *t;
    }

    // One block: what the host staged, each Game Boy's MI.OUT a byte a message at its frame as its serialOut carries
    // it, then the block as the engine runs it (the host outputs cleared first, as the engine clears them), and every
    // output recorded.
    void block(const BlockIn& in) {
        stagedMidi = in.midi;
        stagedController = in.controller;
        connected = in.connected;
        for (const std::uint32_t gb : structure.gameBoys) {
            Feed& feed = node<Feed>("mi-out-" + std::to_string(gb));
            for (const Captured& c : in.serialOut)
                if (c.system == gb)
                    for (std::size_t i = 0; i < c.bytes.size(); ++i)
                        feed.next[Feed::Bytes].push_back({c.first + static_cast<std::uint32_t>(i) * c.stride, {c.bytes[i]}, 0});
        }
        midiOut.clear();
        controllerOut.clear();
        scripts.beginBlock();
        host->processBlock(in.frames, rp_transport{kRate, in.tempo, in.ppq, in.playing ? std::uint32_t{RP_TRANSPORT_PLAYING} : 0u});
        record(in.frames);
        unsentByKernel.clear();
        ++blocks;
    }
    void run(BlockIn in, int count) {
        for (int i = 0; i < count; ++i) {
            block(in);
            in.midi.clear();
            in.controller.clear();
            in.serialOut.clear();
            if (in.playing) in.ppq += in.tempo / 60.0 * in.frames / kRate;
        }
    }

    // The block's outputs into the digest: each system's four, in structure order, then the host's two.
    void record(std::uint32_t frames) {
        digest.u32(static_cast<std::uint32_t>(blocks));
        digest.u32(frames);
        static const std::vector<Message> none;
        for (const std::uint32_t id : structure.systems) {
            digest.u32(id);
            const auto unsent = [&](const char* input) {
                const auto key = std::make_pair(id, std::string(input));
                return std::find(unsentByKernel.begin(), unsentByKernel.end(), key) != unsentByKernel.end();
            };
            const Probe& in = node<Probe>("in-" + std::to_string(id));
            const Probe& io = node<Probe>("io-" + std::to_string(id));
            // A byte a message, as when recorded.
            const std::vector<Message> serialIn = unsent("serialIn") ? none : serialBytes(in.bytes);
            const std::vector<Message>& midi = unsent("midi") ? none : in.events;
            const std::vector<Message>& bytes = unsent("bytes") ? none : io.bytes;
            const std::vector<Message>& buttons = unsent("buttons") ? none : io.events;
            for (const auto* ms : {&serialIn, &midi, &bytes, &buttons}) digest.messages(*ms);
            sent.serialIn += static_cast<int>(serialIn.size());
            sent.midi += static_cast<int>(midi.size());
            sent.bytes += static_cast<int>(bytes.size());
            sent.buttons += static_cast<int>(buttons.size());
        }
        digest.u32(static_cast<std::uint32_t>(midiOut.size()));
        for (const Out& m : midiOut) {
            digest.u32(m.system);
            digest.u32(m.frame);
            digest.data(m.data);
        }
        digest.u32(static_cast<std::uint32_t>(controllerOut.size()));
        for (const Bytes& c : controllerOut) digest.data(c);
        sent.midiOut += static_cast<int>(midiOut.size());
        sent.controllerOut += static_cast<int>(controllerOut.size());
        sent.digest = digest.h;
    }

    // ---- the host I/O stand-in -----------------------------------------------------------------------------------
    static ProtocolRig& of(void* ctx) { return *static_cast<ProtocolRig*>(ctx); }
    // As the engine's: the staging stable-sorted by frame (design section 3.4), then cleared.
    static void takeMidi(void* ctx, rp_queue* out) {
        auto& staged = of(ctx).stagedMidi;
        std::stable_sort(staged.begin(), staged.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [frame, data] : staged) rp::graph::pushMessage(*out, frame, data);
        staged.clear();
    }
    static void takeController(void* ctx, rp_queue* out) {
        for (const Bytes& data : of(ctx).stagedController) rp::graph::pushMessage(*out, 0, data);
        of(ctx).stagedController.clear();
    }
    static bool isConnected(void* ctx) { return of(ctx).connected; }
    static void recordMidiOut(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
        of(ctx).midiOut.push_back({system, frame, Bytes(data, data + size)});
    }
    static void recordControllerOut(void* ctx, const std::uint8_t* data, std::size_t size) {
        of(ctx).controllerOut.emplace_back(data, data + size);
    }
    static void noCart(void*, std::uint32_t, const std::uint8_t*, std::size_t, bool) {}

    // True before an audio thread starts running blocks, false after it has stopped, as the Engine does.
    void setAudioThreadOwns(bool owns) {
        if (owns) scripts.setAudioThreadOwns(true);
        host->setAudioThreadOwns(owns);
        if (!owns) scripts.setAudioThreadOwns(false);
    }

    // What the case sent is what it sent when recorded.
    void matches(const Sent& recorded) {
        INFO("sent " << text(sent) << ", recorded " << text(recorded));
        CHECK(sent == recorded);
    }

    // Nothing dropped, no script node faulted, and `throws` caught in all.
    void finish(std::uint32_t throws = 0) {
        CHECK(host->droppedMessages() == 0);
        for (const std::string& id : scriptNodes(structure)) {
            INFO(id);
            CHECK_FALSE(host->isFaulted(id));
        }
        CHECK(scripts.throwCount() == throws);
    }
};

// A frame-flagged MI.OUT byte as LSDj's link carries it: a flag bit, then the protocol byte's seven bits.
std::uint8_t framed(std::uint8_t command) { return static_cast<std::uint8_t>(0x80 | (command & 0x7f)); }

// A press of the pad at (x, y), (0, 0) top left, as a Launchpad in Programmer mode sends it (padIndex in
// src/launchpad/profile.ts).
Bytes pad(int x, int y, int velocity = 100) {
    return {0x90, static_cast<std::uint8_t>((7 - y) * 10 + x + 11), static_cast<std::uint8_t>(velocity)};
}

}  // namespace

TEST_CASE("the per-role graph hands every system its host MIDI, under every routing", "[engine-graph][protocol-nodes]") {
    // mGB forwards each byte to its serial port, the NES each message to its FIFO, a SysEx (longer than a MidiEvent)
    // included. The plugin stages a block's DAW events, then a moved automation slot's CC at frame 0.
    ProtocolRig rig(structure("mgb-and-n8/sendToAll"));  // mGB on Game Boy 1, the N8 on NES 2
    rig.block({.midi = {{100, on(0, 60)}, {300, on(1, 62)}, {0, cc(0, 7, 100)}}});
    // In frame order (rp.host.midi-in's sort), to both: each byte to mGB's serial port, each message to the NES.
    std::vector<Message> bytes, messages;
    for (const auto& [frame, m] : std::vector<std::pair<std::uint32_t, Bytes>>{{0, cc(0, 7, 100)}, {100, on(0, 60)}, {300, on(1, 62)}}) {
        for (const std::uint8_t b : m) bytes.push_back({frame, {b}, 0});
        messages.push_back({frame, m, 0});
    }
    CHECK(serialBytes(rig.node<Probe>("in-1").bytes) == bytes);
    CHECK(rig.node<Probe>("in-2").events == messages);
    rig.block({.midi = {{5, off(0, 60)}, {5, on(0, 64)}, {5, on(2, 67)}, {1023, off(1, 62)}}});
    rig.block({.midi = {{10, {0xF0, 0x7D, 1, 2, 3, 4, 5, 6, 7, 0xF7}}, {0, {0xF8}}, {10, {0xFA}}}});
    for (std::uint32_t frames : {1u, 1u, 33u, 1u, 222u, 1u, 7u, 4096u, 2u})
        rig.block({.frames = frames, .midi = {{0, on(3, 40 + static_cast<int>(frames % 50))}, {frames - 1, off(3, 40)}}});
    rig.run({.frames = 8192}, 2);  // nothing staged
    // Each routing, a structure change that keeps every node's ports (a swap).
    for (const char* mode : {"oneChannelPerInstance", "midiChannelToInstance", "fourChannelsPerInstance"}) {
        INFO(mode);
        rig.restructure(structure(std::string("mgb-and-n8/") + mode));
        for (int ch = 0; ch < 8; ++ch) rig.block({.frames = 512, .midi = {{static_cast<std::uint32_t>(ch * 50), on(ch, 50 + ch)}}});
    }
    rig.matches({123, 40, 0, 0, 0, 0, 0xf64c0bc85842f369ULL});
    rig.finish();
}

TEST_CASE("the per-role graph forwards a block that fills rp.host.midi-in, all of it, to a Game Boy's serial port and a NES",
          "[engine-graph][protocol-nodes]") {
    // The one place host MIDI is dropped is rp.host.midi-in's output: every output that forwards it on (a routing
    // output, mGB's serial in, a NES's MIDI) holds at least as much. So a block that fills it to the message and the
    // byte (two dense curves, a CC and a pitch bend, notes, and a SysEx, a wave upload, taking the rest of the bytes)
    // reaches mGB's serial input byte for byte and the NES's MIDI message for message.
    using rp::host::kHostMidiInBytes;
    using rp::host::kHostMidiInMessages;
    constexpr std::uint32_t kFrames = 8192;
    const std::uint32_t sysExAt = kHostMidiInMessages / 2;
    const std::uint32_t sysExBytes = kHostMidiInBytes - (kHostMidiInMessages - 1) * 3;
    REQUIRE(sysExBytes > 3);
    std::vector<std::pair<std::uint32_t, Bytes>> midi;
    for (std::uint32_t i = 0; i < kHostMidiInMessages; ++i) {
        const std::uint32_t frame = i * kFrames / kHostMidiInMessages;
        const int v = static_cast<int>(i & 0x7f);
        if (i == sysExAt) {
            Bytes sysEx(sysExBytes);
            for (std::uint32_t j = 0; j < sysExBytes; ++j) sysEx[j] = static_cast<std::uint8_t>(j & 0x7f);
            sysEx.front() = 0xF0;
            sysEx.back() = 0xF7;
            midi.push_back({frame, std::move(sysEx)});
            continue;
        }
        switch (i % 4) {
            case 0: midi.push_back({frame, cc(0, 1, v)}); break;
            case 1: midi.push_back({frame, {0xE1, static_cast<std::uint8_t>(v), static_cast<std::uint8_t>((i >> 7) & 0x7f)}}); break;
            case 2: midi.push_back({frame, on(2, 36 + v % 48)}); break;
            default: midi.push_back({frame, off(2, 36 + (v - 1) % 48)}); break;
        }
    }
    std::vector<Message> bytes, messages;
    for (const auto& [frame, data] : midi) {
        for (const std::uint8_t b : data) bytes.push_back({frame, {b}, 0});
        messages.push_back({frame, data, 0});
    }
    REQUIRE(messages.size() == kHostMidiInMessages);
    REQUIRE(bytes.size() == kHostMidiInBytes);

    ProtocolRig rig(structure("mgb-and-n8/sendToAll"));
    rig.block({.frames = kFrames, .midi = midi});
    const std::vector<Message> serialIn = serialBytes(rig.node<Probe>("in-1").bytes);
    const std::vector<Message>& nesMidi = rig.node<Probe>("in-2").events;
    CHECK(rig.host->droppedMessages("midi-in") == 0);
    for (const std::string& id : rig.scriptNodes(rig.structure)) {
        INFO(id);
        CHECK(rig.host->droppedMessages(id) == 0);
    }
    CHECK(serialIn.size() == bytes.size());
    CHECK(rig.node<Probe>("in-1").bytes.size() == messages.size());  // a run for each message, each at its own frame
    CHECK(nesMidi.size() == messages.size());
    CHECK(text(serialIn) == text(bytes));
    CHECK(text(nesMidi) == text(messages));
    rig.finish();
}

TEST_CASE("the per-role graph drives each LSDj sync mode, MI.OUT and master sync among them", "[engine-graph][protocol-nodes]") {
    // Game Boys 1 to 9 in MIDI sync auto-starting, MIDI sync on every second tick, Arduinoboy auto-starting, MI.MAP,
    // keyboard, passthrough, MI.OUT, master sync and off.
    ProtocolRig rig(structure("lsdj-modes"));
    // Stopped: notes only (MI.MAP rows, Arduinoboy rows and its play flag, keyboard scancodes, passthrough bytes).
    rig.block({.midi = {{0, on(0, 5)}, {40, on(0, 24)}, {41, on(0, 40)}, {90, on(0, 48)}, {91, on(1, 9)}}});
    rig.block({.midi = {{3, off(0, 5)}, {3, off(1, 9)}, {700, on(0, 72)}, {701, on(0, 27)}}});
    // The transport starts with a row note in the block: the Arduinoboy START and the clocks go out at frame 0 after
    // the row byte, and MI.MAP's clocks after its row, each raised to the row byte's frame on the wire (a frame-ordered
    // queue), which SameBoy's serial queue, releasing only its front byte, cannot tell.
    rig.block({.ppq = 0.0, .playing = true, .midi = {{200, on(0, 31)}, {200, on(0, 7)}}});
    BlockIn playing{.frames = 1024, .ppq = 1024.0 / kBeat, .playing = true};
    rig.run(playing, 30);
    // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run (a playing LSDj's bytes
    // a few hundred frames apart), a stopped LSDj's silence, whose 200 ms (8820 frames) pass in the ninth quiet block
    // and send its stop, then a run again.
    playing.ppq = 31 * 1024.0 / kBeat;
    for (auto captured : {std::vector<Captured>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
                                                {8, {5, 5, 5}, 300}},
                          {{7, {0x80, framed(0x74), framed(64)}}, {8, {}}},
                          {}, {}, {}, {}, {}, {}, {},
                          {{7, {framed(0x7e)}}},
                          {{8, {7}}}}) {
        playing.serialOut = captured;
        rig.block(playing);
        playing.ppq += 1024.0 / kBeat;
    }
    // A seek, a faster tempo across 1-frame and FL-style ragged blocks, then the transport stops.
    BlockIn fast{.ppq = 32.0, .tempo = 174.0, .playing = true};
    for (std::uint32_t frames : {33u, 1u, 222u, 1u, 7u, 4096u, 2u, 1u, 1u, 1u, 8192u}) {
        fast.frames = frames;
        rig.block(fast);
        fast.ppq += fast.tempo / 60.0 * frames / kRate;
    }
    rig.block({.ppq = fast.ppq, .tempo = 174.0, .playing = false, .midi = {{0, on(0, 25)}, {0, on(0, 26)}}});
    rig.run({.frames = 2048, .ppq = fast.ppq}, 2);
    rig.matches({269, 0, 0, 4, 16, 0, 0xef0d11f87eff9493ULL});  // the buttons: autoStart's START press and release
    rig.finish();
}

TEST_CASE("the per-role graph drives risa and smsggdj host sync, and a NES's host MIDI", "[engine-graph][protocol-nodes]") {
    ProtocolRig rig(structure("risa-and-sms"));  // NES 1 on the N8 and risa, smsggdj on SMS 2 and Game Gear 3
    rig.block({.midi = {{12, on(0, 60)}, {500, off(0, 60)}}});
    // A start partway into a phrase (an arm and START, flushing), clocks, a seek (a re-arm), an exact loop back to
    // the same place, a stop, and a restart at a new tempo.
    BlockIn play{.frames = 1024, .ppq = 3.25, .playing = true};
    rig.run(play, 8);
    rig.block({.ppq = 16.0, .playing = true, .midi = {{0, on(1, 50)}}});
    rig.block({.ppq = 16.0, .playing = true});
    rig.run({.frames = 777, .ppq = 16.0 + 1024.0 / kBeat, .playing = true}, 5);
    rig.block({.ppq = 20.0, .playing = false});
    BlockIn again{.ppq = 4.0, .tempo = 96.0, .playing = true};
    for (std::uint32_t frames : {1u, 1u, 33u, 222u, 7u, 4096u}) {
        again.frames = frames;
        rig.block(again);
        again.ppq += again.tempo / 60.0 * frames / kRate;
    }
    rig.matches({0, 3, 64, 0, 0, 0, 0x3db831405226989bULL});
    rig.finish();
}

TEST_CASE("the per-role graph runs a controller session: its surface, its launches, its LEDs", "[engine-graph][protocol-nodes]") {
    // A Launchpad driving an emulated cart in MI.MAP (whose clock and launches go to its serial port), beside a Game
    // Boy decoding MI.OUT: the controller on Game Boy 7, quantised to the bar, and Game Boy 8 in MI.OUT. Both feed one
    // rp.host.midi-out, but only Game Boy 8 sends it anything: the one press the controller sends towards the host,
    // below, is cued for a bar past the last block (the next case's presses reach the host).
    const auto target = [](const std::string& t) { return structure("controller/" + t); };
    ProtocolRig rig(target("system"));
    rig.block({.controller = {pad(0, 3)}});  // pressed with no surface attached
    rig.block({.connected = true});          // attached: the session takes it (Programmer mode, a full paint)
    rig.run({.connected = true}, 2);
    BlockIn play{.frames = 1024, .ppq = 0.0, .playing = true, .connected = true};
    play.controller = {pad(0, 0)};  // nothing playing yet: launched at once
    rig.block(play);
    play.ppq += 1024.0 / kBeat;
    play.controller = {pad(0, 5), pad(4, 2)};  // cued for the bar
    for (int i = 0; i < 100; ++i) {
        rig.block(play);
        play.controller.clear();
        play.ppq += 1024.0 / kBeat;
    }
    rig.block({.ppq = play.ppq, .playing = true, .connected = false});  // detached
    rig.block({.ppq = play.ppq, .playing = true, .connected = true});   // and back: repainted
    // Its launches to a real Game Boy's MIDI out instead (a new session, the predictor carried over), while the other
    // Game Boy's MI.OUT is decoded in the same blocks. Its press waits for a bar no block reaches, so every host MIDI
    // out message is Game Boy 8's.
    rig.restructure(target("midiOut"));
    play.controller = {pad(0, 6)};
    for (int i = 0; i < 40; ++i) {
        play.serialOut = {{8, {framed(0x7f), framed(0x71), framed(60)}}};
        rig.block(play);
        for (const ProtocolRig::Out& m : rig.midiOut) CHECK(m.system == 8);
        play.controller.clear();
        play.ppq += 1024.0 / kBeat;
    }
    rig.matches({161, 0, 0, 0, 80, 35, 0x097e9ae8508f0a25ULL});
    rig.finish();
}

TEST_CASE("the per-role graph puts a controller's launches after the routed MIDI, and its host MIDI out before the "
          "systems'",
          "[engine-graph][protocol-nodes]") {
    // The two orders the case above leaves without a message (docs/modular-graph-phase-b3b-design.md section 5, items 2
    // and 3: it stages no host MIDI, and its one launch to the host is cued past its last block), and the controller's
    // default target (item 6). Launched at once, each press lands in the block it is pressed in: into the first
    // system's MI.MAP, behind a row the host sent at frame 300, then, its target switched, out of the host in a block
    // where two Game Boys send host MIDI too (MI.OUT, and a master's clocks). Its value was recorded from the kernel
    // node when the per-role graph was added, after the old kernel stage had gone, and no case run beside that stage
    // sent a message through either order: the stage-era proof never reached them, and the value holds the per-role
    // graph only to what the kernel node did. So both orders are checked directly as well.
    //
    // The controller on the first system, launching at once; Game Boy 1 in MI.MAP, 2 in MI.OUT, 3 in master sync.
    const auto target = [](const std::string& t) { return structure("controller-orders/" + t); };
    ProtocolRig rig(target("system"));
    BlockIn play{.frames = 1024, .ppq = 0.0, .playing = true, .connected = true};
    std::vector<std::vector<Message>> cartSerialIn;  // Game Boy 1's serial input, a block a press, while it is targeted
    std::vector<Bytes>                hostLaunches;  // the controller's host MIDI out, a block a press, after
    const auto press = [&](int blocks, bool toHost) {
        for (int i = 0; i < blocks; ++i) {
            play.midi = {{300, on(0, 20 + i)}};
            play.controller = {pad(i % 8, i % 4)};
            play.serialOut = {{2, {framed(0x7f), framed(0x71), framed(60)}}, {3, {5, 5, 5}, 300}};
            rig.block(play);
            play.ppq += 1024.0 / kBeat;
            if (!toHost) {
                cartSerialIn.push_back(serialBytes(rig.node<Probe>("in-1").bytes));
                continue;
            }
            // Host MIDI out: the controller's launch first, under the system its default target resolved to (Game Boy
            // 1), then Game Boy 2's decoded MI.OUT, then Game Boy 3's clocks.
            std::vector<std::uint32_t> systems;
            for (const ProtocolRig::Out& m : rig.midiOut) systems.push_back(m.system);
            INFO("press " << i);
            REQUIRE_FALSE(rig.midiOut.empty());
            hostLaunches.push_back(rig.midiOut.front().data);
            CHECK(systems.front() == 1);
            CHECK(std::is_sorted(systems.begin(), systems.end()));
            CHECK(std::count(systems.begin(), systems.end(), 1u) == 1);
            CHECK(std::count(systems.begin(), systems.end(), 2u) > 0);
            CHECK(std::count(systems.begin(), systems.end(), 3u) > 0);
        }
    };
    press(6, false);
    rig.restructure(target("midiOut"));
    press(6, true);
    // The same presses launch the same rows: out of the host, a NoteOn whose key is the row; into the cart, that row's
    // byte, behind the row the host's note sent (20 + i), at its frame, 300, where the launch came at frame 0 and the
    // frame-ordered queue raised it.
    REQUIRE(cartSerialIn.size() == 6);
    REQUIRE(hostLaunches.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        INFO("press " << i);
        REQUIRE(hostLaunches[i].size() == 3);
        CHECK(hostLaunches[i][0] == 0x90);
        REQUIRE(cartSerialIn[i].size() >= 2);
        CHECK(cartSerialIn[i][0] == Message{300, {static_cast<std::uint8_t>(20 + i)}, 0});
        CHECK(cartSerialIn[i][1] == Message{300, {hostLaunches[i][1]}, 0});
    }
    rig.matches({32, 0, 0, 0, 68, 84, 0xeb884025953336a5ULL});
    rig.finish();
}

TEST_CASE("the per-role graph keeps its state across structure changes: survivors go on, a system added again is fresh",
          "[engine-graph][protocol-nodes]") {
    // A survivor's nodes are kept through each change (the rig checks), the routing node reshaped as the systems move,
    // so a clock that survives does not restart; a system added, or removed and added again, has new nodes (the rig
    // checks), so it starts afresh, as the kernel pruned a removed id's state.
    // Game Boy 1 in MIDI sync, 2 on mGB, 3 in Arduinoboy sync.
    ProtocolRig rig(structure("state/first"));
    BlockIn play{.frames = 1024, .ppq = 0.5, .playing = true, .midi = {{0, on(0, 24)}}};  // Arduinoboy: play
    rig.block(play);
    play.midi.clear();
    const auto step = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            play.ppq += 1024.0 / kBeat;
            rig.block(play);
        }
    };
    step(4);
    // A system added, which shifts no one's routing.
    rig.restructure(structure("state/nes-added"));  // NES 4, on the N8 and risa
    play.midi = {{10, on(0, 61)}};
    step(4);
    play.midi.clear();
    // A block is a little over one tick at 24 PPQN, so a playing clock sends at least one every block.
    const auto clocks = [&](std::uint32_t id) {
        const auto sent = serialBytes(rig.node<Probe>("in-" + std::to_string(id)).bytes);
        return std::count_if(sent.begin(), sent.end(), [](const Message& m) { return m.data == Bytes{0xF8}; });
    };
    CHECK(clocks(3) > 0);
    // Arduinoboy's system removed, then added again: its play flag is gone, so it clocks nothing until note 24.
    rig.restructure(structure("state/arduinoboy-removed"));
    step(3);
    rig.restructure(structure("state/arduinoboy-again"));
    step(3);
    CHECK(clocks(1) > 0);
    CHECK(clocks(3) == 0);
    // A feature-role edit on a survivor (its clock divided, its tick counter kept), and a new order, which moves
    // positional routing.
    rig.restructure(structure("state/divided-and-reordered"));  // 1 on every fourth tick; then 3, 4 and 2
    for (int ch = 0; ch < 4; ++ch) {
        play.midi = {{static_cast<std::uint32_t>(ch), on(ch, 70)}};
        step(1);
    }
    // Everything removed, then one system back.
    rig.restructure(structure("empty"));
    step(2);
    rig.restructure(structure("state/mgb-alone"));
    play.midi = {{0, on(0, 65)}};
    step(1);
    rig.matches({57, 5, 18, 0, 0, 0, 0xd28fffd230a8c468ULL});
    rig.finish();
}

TEST_CASE("the per-role graph keeps a controller's session through a system added behind it: its cue fires, the device "
          "is not taken again",
          "[engine-graph][protocol-nodes]") {
    // Every structural edit is a structure push, so a session rebuilt at each would lose its pending cue and its shadow
    // of the device, which it would then take and repaint whole for no reason (what project-ctx's "state survives a
    // structure push" held the kernel to). Its node is kept through the change by its id and type, whatever its
    // settings, since the controller's type reshapes (the rig checks the instance); its settings are the same before
    // and after (its target, "0 is the first system", still resolves to Game Boy 1), which is what keeps the session in
    // it from being rebuilt. What is checked here is that its state went with it. Nothing here was recorded from the
    // kernel.
    ProtocolRig rig(structure("controller-survives/one-cart"));  // the controller on Game Boy 1's MI.MAP, by the bar
    // MI.MAP's row bytes in a system's serial input: everything but its tick and its note-off.
    const auto rows = [&](std::uint32_t id) {
        std::vector<Message> out;
        for (const Message& m : serialBytes(rig.node<Probe>("in-" + std::to_string(id)).bytes))
            if (m.data[0] != 0xFF && m.data[0] != 0xFE) out.push_back(m);
        return out;
    };
    BlockIn play{.frames = 4096, .ppq = 0.0, .playing = true, .controller = {pad(0, 0)}, .connected = true};
    rig.block(play);  // attached, and nothing playing yet: the press launches at once
    // A fresh session takes the device: Programmer mode first, then a full paint.
    const std::vector<Bytes> hello = rig.controllerOut;
    REQUIRE_FALSE(hello.empty());
    CHECK(rows(1).size() == 1);
    play.ppq += 4096.0 / kBeat;
    play.controller = {pad(0, 5)};  // cued for the bar, at ppq 4
    rig.block(play);
    play.controller.clear();
    CHECK(rows(1).empty());
    rig.restructure(structure("controller-survives/system-added"));  // mGB on Game Boy 2
    // Until the bar, no launch; then the cued one, in the first block that starts on or past it (the session reads the
    // transport at a block's start); and the device is never taken again.
    int launched = 0;
    for (int i = 0; i < 24; ++i) {
        const bool first = play.ppq < 4.0 && play.ppq + 4096.0 / kBeat >= 4.0;
        play.ppq += 4096.0 / kBeat;
        rig.block(play);
        INFO("block at ppq " << play.ppq);
        CHECK(rows(1).size() == (first ? 1u : 0u));
        launched += static_cast<int>(rows(1).size());
        for (const Bytes& m : rig.controllerOut) CHECK(m != hello.front());
    }
    CHECK(launched == 1);
    CHECK(rows(2).empty());
    rig.finish();
}

TEST_CASE("a throw in a protocol node keeps what was sent before it and runs the next block", "[engine-graph][protocol-nodes]") {
    // No shipped node throws on what its settings accept, so the throw is injected: run after the bundle, in both
    // runtimes, Math.ceil throws for one value, which walkTicks reaches when the second system's clock first syncs,
    // at ppq 10.5 (tick 252), before that node sends anything in the block. The first system's mGB node forwards its
    // MIDI that block, as the kernel did ahead of the throw.
    //
    // The recorded value is the kernel's, in which the throw also stopped every system after the one it was in, so the
    // third system sent nothing that block. A throw stops only the node it is in (the design's section 5): the third
    // system's mGB node, planned after the LSDj node as buildGraph places it (the plan-order case holds that), runs
    // that block and forwards its MIDI to its serial input, which is checked here, and that block's digest takes that
    // one input as the kernel left it, empty (the third system's others are empty under both). Everything else is held
    // to the recorded values, the throw count included.
    //
    // What a throw keeps here is what the nodes around it sent. That a node's own output up to its throw stands, and
    // that it runs again the next block, is the script host's rule for every script node, held by ScriptNodes.test.cpp
    // ("a throw in a script node is counted and it runs on; ..."): this node has sent nothing when it throws.
    const std::string inject = R"JS(
      (function () {
        var ceil = Math.ceil;
        Math.ceil = function (x) { if (x === 252) throw new Error("injected"); return ceil(x); };
      })();
    )JS";
    ProtocolRig rig(structure("throw"), inject);  // mGB on Game Boys 1 and 3, MIDI sync on 2
    rig.block({.midi = {{0, on(0, 60)}}});
    rig.unsentByKernel = {{3, "serialIn"}};
    rig.block({.ppq = 10.5, .playing = true, .midi = {{4, on(0, 62)}}});
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(rig.node<Probe>("in-2").bytes.empty());
    CHECK(text(serialBytes(rig.node<Probe>("in-3").bytes)) == "4:90 4:3e 4:64 ");
    rig.run({.ppq = 10.5 + 1024.0 / kBeat, .playing = true, .midi = {{4, on(0, 64)}}}, 3);
    rig.matches({18, 0, 0, 0, 0, 0, 0x420d378cfb9af0faULL});
    rig.finish(1);
}

TEST_CASE("each structure's script nodes run in the order buildGraph's document places them",
          "[engine-graph][protocol-nodes]") {
    // Production plans the script nodes in their document order: nothing a script node reads comes from a node after it
    // but over a feedback wire, which orders nothing (test/graph/build-graph-role-goldens.test.ts holds every golden
    // document to that), and the planner breaks ties by document order. The rig must run
    // them in that order too, or a case that holds what one node does to the nodes after it (the throw) holds nothing.
    // So each structure is planned and run a block with every script node's process recorded as it is called (the wrap
    // injected below, through __probe), and the record is held to buildGraph's script nodes in document order, each by
    // its type and settings: two nodes of one type with one settings (the throw structure's two mGB nodes) are not told
    // apart, and neither reads the other.
    const std::string recordOrder = R"JS(
      (function () {
        var defs = globalThis.__rp_scriptNodes;
        for (var i = 0; i < defs.length; i++) (function (d) {
          var create = d.create;
          d.create = function (ctx) {
            var node = create.call(this, ctx);
            var process = node.process;
            var tag = d.id + " " + JSON.stringify(ctx.settings);
            node.process = function (b) { __probe(tag); return process.call(node, b); };
            return node;
          };
        })(defs[i]);
      })();
    )JS";
    const std::vector<std::string> names = rigStructureNames();
    REQUIRE(names.size() > 20);
    for (const std::string& name : names) {
        INFO(name);
        ProtocolRig rig(structure(name), recordOrder);
        rig.block({});
        std::vector<std::string> ran, placed;
        for (const auto& [tag, thread] : rig.log.calls) {
            const auto space = tag.find(' ');
            const auto settings = rp::graph::parseJson(tag.substr(space + 1));
            REQUIRE(settings);
            ran.push_back(tag.substr(0, space) + " " + rp::graph::canonicalJson(*settings));
        }
        for (const PlacedNode& n : rig.structure.nodes)
            if (rig.scriptTypes.count(n.type))
                placed.push_back(n.type + " " + (n.settings.empty() ? "{}" : n.settings));
        CHECK(ran == placed);
        rig.finish();
    }
}

TEST_CASE("the per-role graph restructures under a running audio thread, the routing's instance kept through every change",
          "[engine-graph][protocol-nodes][stress]") {
    // The threading of the real nodes: their settings committed on the audio thread at each swap, and a system's nodes
    // created and released there as it comes and goes, the routing node reshaped at each, while the control thread
    // plans the next and reclaims the last. The structures include a controller with no system, whose host MIDI goes
    // out under 4294967295 (the kernel's -1) and whose node's system is 0, its none.
    const std::vector<Structure> structures{structure("stress/lsdj-and-nes"), structure("stress/controller"),
                                            structure("stress/mgb-by-channel"), structure("empty"),
                                            structure("stress/controller-no-systems")};
    ProtocolRig rig(structures[0]);
    rig.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::thread audio([&] {
        double ppq = 0.0;
        while (running.load()) {
            rig.scripts.beginBlock();
            rig.host->processBlock(512, rp_transport{kRate, 120.0, ppq, RP_TRANSPORT_PLAYING});
            ppq += 512.0 / kBeat;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    int refused = 0;
    for (int i = 0; i < 200; ++i) {
        rig.structure = structures[static_cast<std::size_t>(i) % structures.size()];
        const auto r = rig.host->setGraph(rig.document());
        if (!r.ok) {
            REQUIRE(r.errors.at(0).code == "busy");
            ++refused;
        }
        rig.host->reclaim();
        rig.scripts.reclaim();
        std::this_thread::sleep_for(std::chrono::microseconds(150));
    }
    running = false;
    audio.join();
    rig.setAudioThreadOwns(false);
    CHECK(refused < 200);
    // Quiescent again: the last structure planned once more, so every node it places keeps the instance it has and the
    // instances live are those nodes', and the node every structure places has kept the one the first plan made.
    rig.restructure(rig.structure);
    CHECK(rig.host->node(rig.keptId) == rig.kept);
    rig.finish();
}
