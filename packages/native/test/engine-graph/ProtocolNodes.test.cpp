// The protocol script nodes (docs/modular-graph-phase-b3b-design.md section 2): the MIDI routing, a controller session
// and each system's protocol nodes, as buildGraph places them, of the production script bundle in a plan, fed by the
// host I/O nodes and a stand-in for the Game Boys' serialOut, what reaches every system input read back by probes, block
// by block.
//
// Each case is held to the values the DSP kernel those nodes replaced was recorded sending: the messages of each kind,
// and a digest of all of them (per block, per system and input, each message's frame, flags and bytes in order; the
// host MIDI out with its system tags in emission order; the control-surface out). A digest hashes what reaches each
// system input and the host outputs, never which node sent it, so a split of the kernel's work over nodes is held to
// what the kernel sent: that is B3b's proof, bit for bit, that splitting the kernel changed nothing a core or the host
// can see. The values have a history. The kernel node (B3a) was run beside the native kernel stage it replaced and
// found equal to it block by block, from c323c7a8 to e3de2e46, the last commit before the stage's deletion; the B3a
// prototype recorded the values from the same cases, and they held as recorded. One was re-recorded since: the LSDj
// modes case, when master sync's stop became a silence rather than a flood (docs/lsdj-master-sync-fix-design.md). One
// case came after the stage had gone, recorded from the kernel node alone: the controller's launches behind routed MIDI
// and its host MIDI out ahead of the systems', two orders no earlier case sends a message through. Then, until B3b
// deleted the kernel node (from the commit that held its recorded digests on the per-role graph too, to the flip that
// had buildGraph place that graph), every case ran on both, the kernel node's graph and this one, each held to the same
// values. So a changed value is a change to what reaches a core or the host: a deliberate one is
// re-recorded from the values a failing case prints.
//
// A serial input is counted and hashed byte by byte, each byte as the one-byte message (its frame, flags 0, one byte)
// the kernel node sent when the values were recorded: a node sends a run of same-frame bytes as one message, which the
// system node applies byte by byte at its frame, so where a run breaks, which no core can see, moves no value. Every
// other input is hashed message for message.
//
// Where the per-role graph differs from the kernel by design (the design's section 5: a throw stops only the node it is
// in), the case says so, checks what the per-role graph does instead, and the digest takes that one difference as the
// kernel had it.
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
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TestNodes.hpp"
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
// ---- structures, as a project holds them ---------------------------------------------------------------------

// A control surface's session as the project holds it, which the document resolves and parses as buildGraph does.
struct Controller {
    std::string   target;    // where its launches go: "system", or "midiOut" (a real Game Boy's Arduinoboy)
    std::uint32_t systemId;  // the system they are for: 0 is the first
    std::string   quantise;  // its app config, { quantise }, as the user set it
};

// The project's MIDI routing and its controller.
struct Project {
    std::string               routing;  // the routing node's mode
    std::optional<Controller> controller;
};

struct Role {
    std::string kind;
    std::string config = "{}";  // JSON, as the store parsed it: a protocol node's settings
};
struct System {
    std::uint32_t     id;
    std::vector<Role> pipeline;
    bool              serialOut = false;  // a Game Boy
};
struct Structure {
    Project             project;
    std::vector<System> systems;
};

// The system roles as the stores parsed them when these cases were written: they have no protocol node, so nothing is
// placed for them (as the kernel ran nothing for them).
const Role kSameBoy{"sameboy", R"({"model":"cgbC","highpass":"accurate","linkGroupId":0,"fastBoot":true,)"
                               R"("colorCorrection":"disabled","dmgPalette":"grey","lightTemperature":0})"};
const Role kMesen{"mesen", R"({"region":"auto","removeSpriteLimit":false,"enableFm":true,"apuLatencyMs":1.4,"s5bNoise":"n8",)"
                           R"("mmc5PhaseReset":"n8","expansionVolume":100,"channelExportMode":"mix","sdRoot":""})"};

// The project's MIDI routing in `mode`, and its controller when one is enabled.
Project routing(const std::string& mode, std::optional<Controller> controller = std::nullopt) {
    return {mode, std::move(controller)};
}
Role lsdj(const std::string& mode, int divisor = 1, bool autoStart = false) {
    return {"lsdj-sync", R"({"mode":")" + mode + R"(","tempoDivisor":)" + std::to_string(divisor) +
                             R"(,"autoStart":)" + (autoStart ? "true" : "false") + "}"};
}
const Role kMgb{"mgb"};
const Role kNesMidi{"nes-n8-midi"};
const Role kRisa{"risa-sync"};
Role sms(const std::string& machine) { return {"sms-sync", R"({"machine":")" + machine + R"("})"}; }

// The lsdj-midimap app's song timing for a song of sixteen one-phrase rows on every channel, as the control plane
// derives it from a battery (songRowTicks).
std::string songRowTicks() {
    std::string row = "[";
    for (int r = 0; r < 16; ++r) row += (r ? "," : "") + std::string("96");
    row += "]";
    return "[" + row + "," + row + "," + row + "," + row + "]";
}
// The lsdj-midimap app driving `target`, for system `systemId` (0 for the first), quantised to `quantise`.
Controller launchpad(const std::string& target, std::uint32_t systemId, const std::string& quantise) {
    return {target, systemId, quantise};
}

System gameBoy(std::uint32_t id, Role feature) { return {id, {kSameBoy, std::move(feature)}, true}; }
System nes(std::uint32_t id, std::vector<Role> features) {
    features.insert(features.begin(), kMesen);
    return {id, std::move(features), false};
}

// ---- the per-role graph, spelled as buildGraph spells it --------------------------------------------------------

// The system inputs, which a system's probes stand in for: serialIn and midi on one probe, bytes and buttons on another.
constexpr const char* kSystemInputs[] = {"serialIn", "midi", "bytes", "buttons"};
std::string probeFor(std::uint32_t system, const std::string& input) {
    const std::string id = std::to_string(system);
    if (input == "serialIn") return "in-" + id + ".bytes";
    if (input == "midi") return "in-" + id + ".events";
    if (input == "bytes") return "io-" + id + ".bytes";
    return "io-" + id + ".events";  // buttons
}

// Each role's protocol node (docs/modular-graph-phase-b3b-design.md section 2): its type, the input its routed MIDI
// arrives on (none for one that reads only the transport), and the system inputs it writes, each from an output of the
// same name. No two roles of a system write one input, so no input of a system is fed twice.
struct ProtocolNode {
    const char*              kind;
    const char*              type;
    const char*              midiIn;
    std::vector<const char*> writes;
};
const std::vector<ProtocolNode> kProtocolNodes{
    {"mgb", "rp.mgb.midi", "midi", {"serialIn"}},
    {"nes-n8-midi", "rp.n8.midi-to-fifo", "midiIn", {"midi"}},
    {"risa-sync", "rp.risa.sync", nullptr, {"bytes"}},
    {"sms-sync", "rp.sms.sync", nullptr, {"bytes"}},
    {"lsdj-sync", "rp.lsdj.sync", "midi", {"serialIn", "buttons"}},
};

// A role's protocol node, or null for a system role, which has none.
const ProtocolNode* protocolNode(const Role& role) {
    for (const ProtocolNode& p : kProtocolNodes)
        if (role.kind == p.kind) return &p;
    INFO(role.kind);
    REQUIRE((role.kind == "sameboy" || role.kind == "mesen"));
    return nullptr;
}

// A system's protocol node: sys-<id>/<role-kind>, the main design's convention.
std::string protocolId(std::uint32_t system, const std::string& kind) { return "sys-" + std::to_string(system) + "/" + kind; }

const Controller* controllerOf(const Structure& s) { return s.project.controller ? &*s.project.controller : nullptr; }

// The system a controller's launches are for, resolved as buildGraph resolves it (controllerTarget,
// src/graph/controller.ts): the chosen one, else the first system, else none, which tags the controller's host MIDI out
// with the kernel's -1.
constexpr std::uint32_t kNoSystem = 0xffffffffu;
std::uint32_t controllerTarget(const Controller& c, const Structure& s) {
    if (c.systemId > 0) return c.systemId;
    return s.systems.empty() ? kNoSystem : s.systems.front().id;
}

// The controller node's settings, as the control plane resolves them: its system (0 for none, the node's "none"), its
// app config as the app's schema parses it, every default filled (lsdj-midimap's follow and page), the timing table and
// no anchor.
std::string controllerSettings(const Controller& c, std::uint32_t target) {
    return R"({"app":"lsdj-midimap","target":")" + c.target + R"(","systemId":)" +
           std::to_string(target == kNoSystem ? 0 : target) + R"(,"appConfig":{"quantise":")" + c.quantise +
           R"(","follow":true,"page":0},"songRowTicks":)" + songRowTicks() + R"(,"anchorRows":[],"anchorSeq":0})";
}

// The script nodes a structure's per-role graph places, in document order: the routing, the controller when the project
// has one, then each system's protocol nodes in slot order, each in its pipeline's order.
std::vector<std::string> perRoleNodes(const Structure& s) {
    std::vector<std::string> ids{"routing"};
    if (controllerOf(s)) ids.emplace_back("controller");
    for (const System& sys : s.systems)
        for (const Role& role : sys.pipeline)
            if (protocolNode(role)) ids.push_back(protocolId(sys.id, role.kind));
    return ids;
}

std::string nodeJson(const std::string& id, const std::string& type, const std::string& settings = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
}
std::string wireJson(const std::string& from, const std::string& to) { return R"({"from":")" + from + R"(","to":")" + to + "\"}"; }
std::string documentJson(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string doc = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) doc += (i ? "," : "") + nodes[i];
    doc += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) doc += (i ? "," : "") + wires[i];
    return doc + "]}";
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
    // Before the graph, so it is destroyed after it: a script node's shim hands its instance back to it.
    ScriptHost                        scripts;
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
    // Systems whose output the next block's digest takes as the kernel sent it, nothing, where the per-role graph sends
    // some by design: a case sets it for one block, and checks what the per-role graph sent instead.
    std::vector<std::uint32_t>        unsentByKernel;

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
    // next structure places that is placed now keeps its instance; and once the replaced plan is reclaimed, the instances
    // live are the nodes placed, one each, so a node placed anew (a system added, or removed and added again) is a new
    // instance and one no longer placed is gone.
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

    // The script nodes placed for a structure.
    static std::vector<std::string> scriptNodes(const Structure& s) { return perRoleNodes(s); }

    void plan() {
        const auto r = host->setGraph(document());
        for (const auto& e : r.errors) UNSCOPED_INFO(e.code << " at " << e.nodeId << "." << e.port << ": " << e.message);
        REQUIRE(r.ok);
    }

    // The per-role graph, as buildGraph places it (docs/modular-graph-phase-b3b-design.md sections 2, 4 and 5): the host
    // I/O nodes, the routing node, the controller node when the project has one, then each system's protocol nodes in
    // slot order, a node for each role in its pipeline that has one, each output into its system's input of that name,
    // which a probe stands in for, with a feed for each Game Boy's MI.OUT. A Game Boy's MI.OUT goes into its lsdj-sync
    // node, and nowhere when it has none, where the kernel took it and ran nothing that read it; the controller's
    // launches go into its target's lsdj-sync node when it drives an emulated cart. rp.host.midi-out takes an input for
    // each node that sends host MIDI, in the kernel's emission order: the controller's first, under the system it
    // resolved (whatever its target: driving a cart, it sends none), then each lsdj-sync node's, in slot order.
    std::string document() const {
        const Structure& s = structure;
        std::vector<std::string> nodes{nodeJson("midi-in", "rp.host.midi-in"), nodeJson("controller-in", "rp.host.controller-in")};
        std::vector<std::string> wires{wireJson("midi-in.midi", "routing.midiIn")};
        // The routing: its mode, and the systems' ids in slot order, each an output, sys-<id>.
        std::string ids;
        for (const System& sys : s.systems) ids += (ids.empty() ? "" : ",") + std::to_string(sys.id);
        nodes.push_back(nodeJson("routing", "rp.midi.route", R"({"mode":")" + s.project.routing + R"(","systems":[)" + ids + "]}"));
        std::vector<std::pair<std::string, std::uint32_t>> producers;  // host MIDI out's, each with its system
        const Controller* controller = controllerOf(s);
        const std::uint32_t target = controller ? controllerTarget(*controller, s) : kNoSystem;
        if (controller) {
            nodes.push_back(nodeJson("controller", "rp.controller.session", controllerSettings(*controller, target)));
            wires.push_back(wireJson("controller-in.midi", "controller.controllerIn"));
            wires.push_back(wireJson("controller-in.connected", "controller.controllerConnected"));
            wires.push_back(wireJson("controller.controllerOut", "controller-out.midi"));
            producers.emplace_back("controller.midiOut", target);
        }
        for (const System& sys : s.systems) {
            for (const Role& role : sys.pipeline) {
                const ProtocolNode* p = protocolNode(role);
                if (!p) continue;
                const std::string n = protocolId(sys.id, role.kind);
                nodes.push_back(nodeJson(n, p->type, role.config));
                if (p->midiIn) wires.push_back(wireJson("routing.sys-" + std::to_string(sys.id), n + "." + p->midiIn));
                for (const char* input : p->writes) wires.push_back(wireJson(n + "." + input, probeFor(sys.id, input)));
                if (role.kind != "lsdj-sync") continue;
                if (sys.serialOut) wires.push_back(wireJson("mi-out-" + std::to_string(sys.id) + ".bytes", n + ".serialOut"));
                if (controller && controller->target == "system" && target == sys.id)
                    wires.push_back(wireJson("controller.launches", n + ".launches"));
                producers.emplace_back(n + ".midiOut", sys.id);
            }
        }
        std::string systems;
        for (std::size_t k = 0; k < producers.size(); ++k) {
            systems += (k ? "," : "") + std::to_string(producers[k].second);
            wires.push_back(wireJson(producers[k].first, "midi-out.in" + std::to_string(k + 1)));
        }
        nodes.push_back(nodeJson("midi-out", "rp.host.midi-out", R"({"systems":[)" + systems + "]}"));
        nodes.push_back(nodeJson("controller-out", "rp.host.controller-out"));
        for (const System& sys : s.systems) {
            const std::string id = std::to_string(sys.id);
            if (sys.serialOut) nodes.push_back(nodeJson("mi-out-" + id, "rp.test.feed"));
            nodes.push_back(nodeJson("in-" + id, "rp.test.probe"));
            nodes.push_back(nodeJson("io-" + id, "rp.test.probe"));
        }
        return documentJson(nodes, wires);
    }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        REQUIRE(t != nullptr);
        return *t;
    }

    // One block: what the host staged, each Game Boy's MI.OUT a byte a message at its frame as its serialOut carries it,
    // then the block as the engine runs it (the host outputs cleared first, as the engine clears them), and every
    // output recorded.
    void block(const BlockIn& in) {
        stagedMidi = in.midi;
        stagedController = in.controller;
        connected = in.connected;
        for (const System& s : structure.systems) {
            if (!s.serialOut) continue;
            Feed& feed = node<Feed>("mi-out-" + std::to_string(s.id));
            for (const Captured& c : in.serialOut)
                if (c.system == s.id)
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
        for (const System& s : structure.systems) {
            digest.u32(s.id);
            const bool unsent = std::find(unsentByKernel.begin(), unsentByKernel.end(), s.id) != unsentByKernel.end();
            const Probe& in = node<Probe>("in-" + std::to_string(s.id));
            const Probe& io = node<Probe>("io-" + std::to_string(s.id));
            const std::vector<Message> serialIn = unsent ? none : serialBytes(in.bytes);  // a byte a message, as when recorded
            const std::vector<Message>& midi = unsent ? none : in.events;
            const std::vector<Message>& bytes = unsent ? none : io.bytes;
            const std::vector<Message>& buttons = unsent ? none : io.events;
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
    ProtocolRig rig({routing("sendToAll"), {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
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
        rig.restructure({routing(mode), {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
        for (int ch = 0; ch < 8; ++ch) rig.block({.frames = 512, .midi = {{static_cast<std::uint32_t>(ch * 50), on(ch, 50 + ch)}}});
    }
    rig.matches({123, 40, 0, 0, 0, 0, 0xf64c0bc85842f369ULL});
    rig.finish();
}

TEST_CASE("the per-role graph forwards a block that fills rp.host.midi-in, all of it, to a Game Boy's serial port and a NES",
          "[engine-graph][protocol-nodes]") {
    // The one place host MIDI is dropped is rp.host.midi-in's output: every output that forwards it on (a routing
    // output, mGB's serial in, a NES's MIDI) holds at least as much. So a block that fills it to the message and the byte (two dense curves, a CC and a pitch bend,
    // notes, and a SysEx, a wave upload, taking the rest of the bytes) reaches mGB's serial input byte for byte and the
    // NES's MIDI message for message.
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

    ProtocolRig rig({routing("sendToAll"), {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
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
    ProtocolRig rig({routing("sendToAll"),
                     {gameBoy(1, lsdj("midiSync", 1, true)), gameBoy(2, lsdj("midiSync", 2)), gameBoy(3, lsdj("midiSyncArduinoboy", 1, true)),
                      gameBoy(4, lsdj("midiMap")), gameBoy(5, lsdj("keyboardMidi")), gameBoy(6, lsdj("midiPassthrough")),
                      gameBoy(7, lsdj("midiOut")), gameBoy(8, lsdj("masterSync")), gameBoy(9, lsdj("off"))}});
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
    ProtocolRig rig({routing("sendToAll"), {nes(1, {kNesMidi, kRisa}), nes(2, {sms("sms")}), nes(3, {sms("gg")})}});
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
    // A Launchpad driving an emulated cart in MI.MAP (whose clock and launches go to its serial port), beside a
    // Game Boy decoding MI.OUT, so the controller's host MIDI out and a system's go out through one rp.host.midi-out.
    const auto structure = [](const std::string& target) {
        return Structure{routing("sendToAll", launchpad(target, 7, "bar")), {gameBoy(7, lsdj("midiMap")), gameBoy(8, lsdj("midiOut"))}};
    };
    ProtocolRig rig(structure("system"));
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
    // Its launches to a real Game Boy's MIDI out instead (a new session, the predictor carried over), while the
    // other Game Boy's MI.OUT is decoded in the same blocks.
    rig.restructure(structure("midiOut"));
    play.controller = {pad(0, 6)};
    for (int i = 0; i < 40; ++i) {
        play.serialOut = {{8, {framed(0x7f), framed(0x71), framed(60)}}};
        rig.block(play);
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
    // where two Game Boys send host MIDI too (MI.OUT, and a master's clocks). Recorded from the kernel node when the
    // per-role graph was added, not beside the old kernel stage, which had gone: so it holds the per-role graph to the
    // kernel node that stage was proven equal to.
    const auto structure = [](const std::string& target) {
        return Structure{routing("sendToAll", launchpad(target, 0, "immediate")),
                         {gameBoy(1, lsdj("midiMap")), gameBoy(2, lsdj("midiOut")), gameBoy(3, lsdj("masterSync"))}};
    };
    ProtocolRig rig(structure("system"));
    BlockIn play{.frames = 1024, .ppq = 0.0, .playing = true, .connected = true};
    const auto press = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            play.midi = {{300, on(0, 20 + i)}};
            play.controller = {pad(i % 8, i % 4)};
            play.serialOut = {{2, {framed(0x7f), framed(0x71), framed(60)}}, {3, {5, 5, 5}, 300}};
            rig.block(play);
            play.ppq += 1024.0 / kBeat;
        }
    };
    press(6);
    rig.restructure(structure("midiOut"));
    press(6);
    rig.matches({32, 0, 0, 0, 68, 84, 0xeb884025953336a5ULL});
    rig.finish();
}

TEST_CASE("the per-role graph keeps its state across structure changes: survivors go on, a system added again is fresh",
          "[engine-graph][protocol-nodes]") {
    // A survivor's nodes are kept through each change (the rig checks), the routing node reshaped as the systems move,
    // so a clock that survives does not restart; a system added, or removed and added again, has new nodes (the rig
    // checks), so it starts afresh, as the kernel pruned a removed id's state.
    const Structure first{routing("sendToAll"), {gameBoy(1, lsdj("midiSync")), gameBoy(2, kMgb), gameBoy(3, lsdj("midiSyncArduinoboy"))}};
    ProtocolRig rig(first);
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
    rig.restructure({first.project, {first.systems[0], first.systems[1], first.systems[2], nes(4, {kNesMidi, kRisa})}});
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
    rig.restructure({first.project, {first.systems[0], first.systems[1], nes(4, {kNesMidi, kRisa})}});
    step(3);
    rig.restructure({first.project, {first.systems[0], first.systems[1], nes(4, {kNesMidi, kRisa}), first.systems[2]}});
    step(3);
    CHECK(clocks(1) > 0);
    CHECK(clocks(3) == 0);
    // A feature-role edit on a survivor (its clock divided, its tick counter kept), and a new order, which moves
    // positional routing.
    rig.restructure({routing("oneChannelPerInstance"),
                     {gameBoy(1, lsdj("midiSync", 4)), first.systems[2], nes(4, {kNesMidi, kRisa}), first.systems[1]}});
    for (int ch = 0; ch < 4; ++ch) {
        play.midi = {{static_cast<std::uint32_t>(ch), on(ch, 70)}};
        step(1);
    }
    // Everything removed, then one system back.
    rig.restructure({first.project, {}});
    step(2);
    rig.restructure({first.project, {first.systems[1]}});
    play.midi = {{0, on(0, 65)}};
    step(1);
    rig.matches({57, 5, 18, 0, 0, 0, 0xd28fffd230a8c468ULL});
    rig.finish();
}

TEST_CASE("a throw in a protocol node keeps what was sent before it and runs the next block", "[engine-graph][protocol-nodes]") {
    // No shipped node throws on what its settings accept, so the throw is injected: run after the bundle, in both
    // runtimes, Math.ceil throws for one value, which walkTicks reaches when the second system's clock first syncs,
    // at ppq 10.5 (tick 252). The first system's mGB node has forwarded its MIDI by then.
    //
    // The recorded value is the kernel's, in which the throw also stopped every system after the one it was in, so the
    // third system sent nothing that block. A throw stops only the node it is in (docs/modular-graph-phase-b3b-design.md
    // section 5): the third system's mGB node runs that block and forwards its MIDI, which is checked here, and that
    // block's digest takes the third system as the kernel left it, with nothing. Everything else is held to the
    // recorded values, the throw count included.
    const std::string inject = R"JS(
      (function () {
        var ceil = Math.ceil;
        Math.ceil = function (x) { if (x === 252) throw new Error("injected"); return ceil(x); };
      })();
    )JS";
    ProtocolRig rig({routing("sendToAll"), {gameBoy(1, kMgb), gameBoy(2, lsdj("midiSync")), gameBoy(3, kMgb)}}, inject);
    rig.block({.midi = {{0, on(0, 60)}}});
    rig.unsentByKernel = {3};
    rig.block({.ppq = 10.5, .playing = true, .midi = {{4, on(0, 62)}}});
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(text(serialBytes(rig.node<Probe>("in-3").bytes)) == "4:90 4:3e 4:64 ");
    rig.run({.ppq = 10.5 + 1024.0 / kBeat, .playing = true, .midi = {{4, on(0, 64)}}}, 3);
    rig.matches({18, 0, 0, 0, 0, 0, 0x420d378cfb9af0faULL});
    rig.finish(1);
}

TEST_CASE("the per-role graph restructures under a running audio thread, the routing's instance kept through every change",
          "[engine-graph][protocol-nodes][stress]") {
    // The threading of the real nodes: their settings committed on the audio thread at each swap, and a system's nodes
    // created and released there as it comes and goes, the routing node reshaped at each, while the control thread plans
    // the next and reclaims the last.
    const std::vector<Structure> structures{
        {routing("sendToAll"), {gameBoy(1, lsdj("midiSync")), nes(2, {kNesMidi, kRisa})}},
        {routing("sendToAll", launchpad("system", 1, "bar")), {gameBoy(1, lsdj("midiMap")), nes(2, {kNesMidi, kRisa}), gameBoy(3, kMgb)}},
        {routing("oneChannelPerInstance"), {gameBoy(3, kMgb)}},
        {routing("sendToAll"), {}},
    };
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
