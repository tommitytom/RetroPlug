// The DSP kernel as a script node (docs/modular-graph-phase-b3a-design.md section 4), held to the kernel stage it
// replaced ("today's" below: the engine runs it no more, but its DspRuntime stays until the kernel stage is deleted).
// The same blocks are driven through today's DspRuntime, loaded with today's kernel bundle (src/dspKernelBundle.ts,
// embedded here alone), and through the kernel node of the production script bundle in a plan, fed by the host I/O
// nodes and a stand-in for the Game Boys' serialOut, its every output read back by probes. Every sink output is
// compared exactly, block by block: per system and port, the frame, bytes, flush flag and order of each message (of
// each byte, on a serial input); the host MIDI out with its system tags in emission order; the control-surface out.
//
// Two differences are the design's, and each is applied to today's side rather than allowed for in the comparison:
//   - host MIDI reaches the node through rp.host.midi-in, which stable-sorts it by frame (section 3.4), so today's
//     kernel is fed the sorted staging;
//   - a queue is frame-ordered: a message pushed earlier than the last one on its port is carried at that one's frame
//     (graph/Queue.hpp pushMessage). Only LSDj's Arduinoboy and MI.MAP modes emit out of order (a row byte at its
//     note's frame, then transport bytes and clocks at frame 0), to a Game Boy's serial input, which SameBoy queues
//     and releases only its front byte from, so a raised frame changes nothing it does. So that port of today's is
//     compared as a wire would carry it, and every other port exactly as today sent it: a raise anywhere else would
//     change what a core does (a NES's or an SMS's FIFO, which NesN8FifoRole and SmsSyncRole stable-sort by frame
//     today, takes a raised byte later). The rig counts the raised bytes by port, and each case says where it expects
//     them.
//
// A serial input is compared as the bytes the system node applies, each at its message's frame, in order: the node
// sends a run of same-frame bytes as one message, where today pushes a byte at a time, and where its runs break is its
// own choice. Every other port is compared message for message.
//
// Last, the node runs on an audio thread while the control thread reshapes it, for the thread sanitizer.
//
// Run via `pnpm test:plugin engine-graph`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
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
#include "host/dsp/DspRuntime.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/script/ScriptHost.hpp"

// Today's kernel bundle (cmake/bundles.cmake, retroplug-dsp-kernel-bundle).
extern "C" {
extern const std::uint8_t  rp_dsp_kernel_bundle[];
extern const std::uint32_t rp_dsp_kernel_bundle_size;
}

using rp::host::ScriptHost;
using rp::host::test::Feed;
using rp::host::test::Message;
using rp::host::test::Probe;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double kRate = 44100.0;
constexpr std::uint32_t kBeat = 22050;  // frames per quarter at 120 bpm
const char* const kKernel = "rp.dsp.kernel";  // KERNEL_NODE_TYPE (src/script/kernelNode.ts)

// ---- structures, spelled as kernelProjection spells them -----------------------------------------------------

struct Role {
    std::string kind;
    std::string config = "{}";  // JSON
};
struct System {
    std::uint32_t     id;
    std::vector<Role> pipeline;
    bool              serialOut = false;  // a Game Boy
};
struct Structure {
    std::vector<Role>   project;
    std::vector<System> systems;
};

// The system roles as the stores parse them: dead bytes to the kernel, carried as it carries them today.
const Role kSameBoy{"sameboy", R"({"model":"cgbC","highpass":"accurate","linkGroupId":0,"fastBoot":true,)"
                               R"("colorCorrection":"disabled","dmgPalette":"grey","lightTemperature":0})"};
const Role kMesen{"mesen", R"({"region":"auto","removeSpriteLimit":false,"enableFm":true,"apuLatencyMs":1.4,"s5bNoise":"n8",)"
                           R"("mmc5PhaseReset":"n8","expansionVolume":100,"channelExportMode":"mix","sdRoot":""})"};

Role routing(const std::string& mode) { return {"midi-routing", R"({"mode":")" + mode + R"("})"}; }
Role lsdj(const std::string& mode, int divisor = 1, bool autoStart = false) {
    return {"lsdj-sync", R"({"mode":")" + mode + R"(","tempoDivisor":)" + std::to_string(divisor) +
                             R"(,"autoStart":)" + (autoStart ? "true" : "false") + "}"};
}
const Role kMgb{"mgb"};
const Role kNesMidi{"nes-n8-midi"};
const Role kRisa{"risa-sync"};
Role sms(const std::string& machine) { return {"sms-sync", R"({"machine":")" + machine + R"("})"}; }

System gameBoy(std::uint32_t id, Role feature) { return {id, {kSameBoy, std::move(feature)}, true}; }
System nes(std::uint32_t id, std::vector<Role> features) {
    features.insert(features.begin(), kMesen);
    return {id, std::move(features), false};
}

std::string rolesJson(const std::vector<Role>& roles) {
    std::string s = "[";
    for (std::size_t i = 0; i < roles.size(); ++i)
        s += (i ? "," : "") + std::string(R"({"kind":")") + roles[i].kind + R"(","config":)" + roles[i].config + "}";
    return s + "]";
}

// Today's KernelStructure (what syncDspFromStore pushes), or the kernel node's settings, which add serialOut.
std::string structureJson(const Structure& s, bool settings) {
    std::string out = R"({"project":)" + rolesJson(s.project) + R"(,"systems":[)";
    for (std::size_t i = 0; i < s.systems.size(); ++i) {
        const System& sys = s.systems[i];
        out += (i ? "," : "") + std::string(R"({"id":)") + std::to_string(sys.id) + R"(,"pipeline":)" + rolesJson(sys.pipeline);
        if (settings) out += std::string(R"(,"serialOut":)") + (sys.serialOut ? "true" : "false");
        out += "}";
    }
    return out + "]}";
}

// ---- one block's input -------------------------------------------------------------------------------------------

struct BlockIn {
    std::uint32_t                                     frames = 1024;
    double                                            ppq = 0.0;
    double                                            tempo = 120.0;
    bool                                              playing = false;
    std::vector<std::pair<std::uint32_t, Bytes>>      midi;        // as a host stages it, in any frame order
    std::vector<Bytes>                                controller;  // control-surface traffic, at frame 0
    bool                                              connected = false;
    std::vector<std::pair<std::uint32_t, Bytes>>      serialOut;   // what each Game Boy captured last block
};

Bytes on(int channel, int key, int velocity = 100) {
    return {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
}
Bytes off(int channel, int key) { return {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(key), 0}; }
Bytes cc(int channel, int number, int value) {
    return {static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)};
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

struct KernelRig {
    Structure  structure;
    DspRuntime today;
    // Before the graph, so it is destroyed after it: the kernel's shim hands its instance back to it.
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
    rp::host::EngineHostIo            hostIo{this, &takeMidi, &takeController, &isConnected, &recordMidiOut, &recordControllerOut, &noCart};
    std::unique_ptr<rp::graph::GraphHost> host;
    rp_node*                          kernel = nullptr;
    int                               blocks = 0;
    // What the comparisons covered: serial-in bytes and messages of each other kind compared, and by port, the
    // serial-in bytes of today's whose frame a queue raises.
    struct {
        int serialIn = 0, midi = 0, bytes = 0, buttons = 0, midiOut = 0, controllerOut = 0;
    } seen;
    std::map<std::string, int> raised;

    // `inject`: JS run after both bundles, in both runtimes.
    explicit KernelRig(Structure s, const std::string& inject = "") : structure(std::move(s)) {
        REQUIRE(today.loadKernel(Bytes(rp_dsp_kernel_bundle, rp_dsp_kernel_bundle + rp_dsp_kernel_bundle_size)));
        std::vector<ScriptHost::Chunk> chunks{ScriptHost::embeddedBundle()};
        Bytes injected;
        if (!inject.empty()) {
            const auto bc = dsp::compileToBytecode(inject);
            REQUIRE(bc.has_value());
            injected = *bc;
            REQUIRE(today.loadKernel(injected));
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
            return std::string(id) == RP_ENGINE_HOST_IO_INTERFACE ? &static_cast<KernelRig*>(ctx)->hostIo : nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
        plan();
        kernel = host->node("kernel");
        REQUIRE(kernel != nullptr);
    }
    ~KernelRig() { host.reset(); }

    // A structure change, as the stores push one: to today's kernel through setSystems, to the node as its settings,
    // which reshape it (the instance kept) whenever its ports move.
    void restructure(Structure next) {
        structure = std::move(next);
        plan();
        CHECK(host->node("kernel") == kernel);
    }

    void plan() {
        const std::string json = structureJson(structure, false);
        REQUIRE(today.setSystems(Bytes(json.begin(), json.end())));
        const auto r = host->setGraph(document());
        for (const auto& e : r.errors) UNSCOPED_INFO(e.code << " at " << e.nodeId << "." << e.port << ": " << e.message);
        REQUIRE(r.ok);
    }

    // The host I/O nodes into the kernel, a feed for each Game Boy's MI.OUT, the kernel, and every output read: a
    // system's serialIn and midi on one probe, its bytes and buttons on another.
    std::string document() const {
        const auto node = [](const std::string& id, const std::string& type, const std::string& settings = "") {
            return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
        };
        const auto wire = [](const std::string& from, const std::string& to) {
            return R"({"from":")" + from + R"(","to":")" + to + "\"}";
        };
        std::vector<std::string> nodes{node("midi-in", "rp.host.midi-in"), node("controller-in", "rp.host.controller-in"),
                                       node("kernel", kKernel, structureJson(structure, true)),
                                       node("midi-out", "rp.host.midi-out"), node("controller-out", "rp.host.controller-out")};
        std::vector<std::string> wires{wire("midi-in.midi", "kernel.midiIn"), wire("controller-in.midi", "kernel.controllerIn"),
                                       wire("controller-in.connected", "kernel.controllerConnected"),
                                       wire("kernel.midiOut", "midi-out.midi"), wire("kernel.controllerOut", "controller-out.midi")};
        for (const System& s : structure.systems) {
            const std::string id = std::to_string(s.id), port = "kernel.sys-" + id;
            if (s.serialOut) {
                nodes.push_back(node("mi-out-" + id, "rp.test.feed"));
                wires.push_back(wire("mi-out-" + id + ".bytes", port + ".serialOut"));
            }
            nodes.push_back(node("in-" + id, "rp.test.probe"));
            nodes.push_back(node("io-" + id, "rp.test.probe"));
            wires.push_back(wire(port + ".serialIn", "in-" + id + ".bytes"));
            wires.push_back(wire(port + ".midi", "in-" + id + ".events"));
            wires.push_back(wire(port + ".bytes", "io-" + id + ".bytes"));
            wires.push_back(wire(port + ".buttons", "io-" + id + ".events"));
        }
        std::string doc = R"({"nodes":[)";
        for (std::size_t i = 0; i < nodes.size(); ++i) doc += (i ? "," : "") + nodes[i];
        doc += R"(],"wires":[)";
        for (std::size_t i = 0; i < wires.size(); ++i) doc += (i ? "," : "") + wires[i];
        return doc + "]}";
    }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        REQUIRE(t != nullptr);
        return *t;
    }

    // One block through both, then every output compared.
    void block(const BlockIn& in) {
        // Today: the staging sorted as rp.host.midi-in sorts it, last block's MI.OUT in slot order, no buttons or
        // keys, and the host outputs cleared first (the engine clears them, at the top of its block).
        std::vector<DspRuntime::MidiIn> midi;
        for (const auto& [frame, data] : in.midi) midi.push_back({frame, data});
        std::stable_sort(midi.begin(), midi.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
        std::vector<DspRuntime::SerialOut> serialOut;
        for (const System& s : structure.systems)
            for (const auto& [id, captured] : in.serialOut)
                if (id == s.id)
                    for (const std::uint8_t b : captured) serialOut.push_back({id, b});
        std::vector<DspRuntime::MidiIn> controller;
        for (const Bytes& c : in.controller) controller.push_back({0, c});
        today.midiOut_.clear();
        today.controllerOut_.clear();
        today.processBlock(midi, {}, {}, serialOut, controller,
                           DspRuntime::BlockInfo{in.frames, kRate, in.tempo, in.ppq, in.playing, in.connected});

        // The node: what the host staged, each Game Boy's MI.OUT a byte a message at frame 0 as its serialOut carries
        // it, then one block as the engine runs it.
        stagedMidi = in.midi;
        stagedController = in.controller;
        connected = in.connected;
        for (const System& s : structure.systems) {
            if (!s.serialOut) continue;
            Feed& feed = node<Feed>("mi-out-" + std::to_string(s.id));
            for (const auto& [id, captured] : in.serialOut)
                if (id == s.id)
                    for (const std::uint8_t b : captured) feed.next[Feed::Bytes].push_back({0, {b}, 0});
        }
        midiOut.clear();
        controllerOut.clear();
        scripts.beginBlock();
        host->processBlock(in.frames, rp_transport{kRate, in.tempo, in.ppq, in.playing ? std::uint32_t{RP_TRANSPORT_PLAYING} : 0u});
        compare();
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

    // Today's bytes for a Game Boy's serial input, as a frame-ordered queue carries them.
    std::vector<Message> carried(std::vector<Message> ms, const std::string& port) {
        for (std::size_t i = 1; i < ms.size(); ++i)
            if (ms[i].frame < ms[i - 1].frame) {
                ms[i].frame = ms[i - 1].frame;
                ++raised[port];
            }
        return ms;
    }
    // The ports whose frames a queue raised.
    std::set<std::string> raisedAt() const {
        std::set<std::string> at;
        for (const auto& [port, count] : raised) at.insert(port);
        return at;
    }

    void compare() {
        INFO("block " << blocks);
        for (const System& s : structure.systems) {
            INFO("system " << s.id);
            std::vector<Message> serialIn, midi, bytes, buttons;
            for (const auto& e : today.serialIn_)
                if (e.system == s.id) serialIn.push_back({e.frame, {e.byte}, 0});
            for (const auto& e : today.coreMidi_)
                if (e.system == s.id) midi.push_back({e.frame, e.data, 0});
            for (const auto& e : today.coreBytes_)
                if (e.system == s.id) bytes.push_back({e.frame, e.data, e.flush ? std::uint32_t{RP_MESSAGE_FLUSH} : 0u});
            for (const auto& e : today.buttonOut_)
                if (e.system == s.id) {
                    CHECK(e.frame == 0);  // the kernel presses at frame 0, and the core takes a press with no frame
                    buttons.push_back({0, {static_cast<std::uint8_t>(e.button), static_cast<std::uint8_t>(e.down ? 1 : 0)}, 0});
                }
            const std::string id = std::to_string(s.id);
            const Probe& in = node<Probe>("in-" + id);
            const Probe& io = node<Probe>("io-" + id);
            CHECK(text(serialBytes(in.bytes)) == text(s.serialOut ? carried(serialIn, "sys-" + id + ".serialIn") : serialIn));
            CHECK(text(in.events) == text(midi));
            CHECK(text(io.bytes) == text(bytes));
            CHECK(text(io.events) == text(buttons));
            seen.serialIn += static_cast<int>(serialIn.size());
            seen.midi += static_cast<int>(midi.size());
            seen.bytes += static_cast<int>(bytes.size());
            seen.buttons += static_cast<int>(buttons.size());
        }
        // Today addressed no system outside the structure, so nothing above went unread.
        const auto known = [&](std::uint32_t id) {
            return std::any_of(structure.systems.begin(), structure.systems.end(), [&](const System& s) { return s.id == id; });
        };
        for (const auto& e : today.serialIn_) CHECK(known(e.system));
        for (const auto& e : today.coreMidi_) CHECK(known(e.system));
        for (const auto& e : today.coreBytes_) CHECK(known(e.system));
        for (const auto& e : today.buttonOut_) CHECK(known(e.system));

        // One port for every system's host MIDI out, in emission order, the system its prefix's.
        std::vector<Message> expected, got;
        std::vector<std::uint32_t> expectedSystems, gotSystems;
        for (const auto& m : today.midiOut_) {
            expected.push_back({m.frame, m.data, 0});
            expectedSystems.push_back(m.system);
        }
        for (const Out& m : midiOut) {
            got.push_back({m.frame, m.data, 0});
            gotSystems.push_back(m.system);
        }
        CHECK(text(got) == text(expected));
        CHECK(gotSystems == expectedSystems);
        CHECK(controllerOut == today.controllerOut_);
        seen.midiOut += static_cast<int>(expected.size());
        seen.controllerOut += static_cast<int>(today.controllerOut_.size());
    }

    // ---- the host I/O stand-in -----------------------------------------------------------------------------------
    static KernelRig& of(void* ctx) { return *static_cast<KernelRig*>(ctx); }
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

    // Nothing dropped, nothing faulted, and `throws` caught in all.
    void finish(std::uint32_t throws = 0) {
        CHECK(host->droppedMessages() == 0);
        CHECK_FALSE(host->isFaulted("kernel"));
        CHECK(scripts.throwCount() == throws);
    }
};

// A frame-flagged MI.OUT byte as LSDj's link carries it: a flag bit, then the protocol byte's seven bits.
std::uint8_t framed(std::uint8_t command) { return static_cast<std::uint8_t>(0x80 | (command & 0x7f)); }

// The lsdj-midimap app's song timing for a song of sixteen one-phrase rows on every channel, as the control plane
// derives it from a battery (songRowTicks).
std::string songRowTicks() {
    std::string row = "[";
    for (int r = 0; r < 16; ++r) row += (r ? "," : "") + std::string("96");
    row += "]";
    return "[" + row + "," + row + "," + row + "," + row + "]";
}
Role launchpad(const std::string& target, std::uint32_t systemId, const std::string& quantise) {
    return {"launchpad", R"({"app":"lsdj-midimap","target":")" + target + R"(","systemId":)" + std::to_string(systemId) +
                             R"(,"appConfig":{"quantise":")" + quantise + R"("},"songRowTicks":)" + songRowTicks() +
                             R"(,"anchorRows":[],"anchorSeq":0})"};
}
// A press of the pad at (x, y), (0, 0) top left, as a Launchpad in Programmer mode sends it (padIndex in
// src/launchpad/profile.ts).
Bytes pad(int x, int y, int velocity = 100) {
    return {0x90, static_cast<std::uint8_t>((7 - y) * 10 + x + 11), static_cast<std::uint8_t>(velocity)};
}

}  // namespace

TEST_CASE("the kernel node hands every system the host MIDI today's kernel hands it, under every routing",
          "[engine-graph][kernel-node]") {
    // mGB forwards each byte to its serial port, the NES each message to its FIFO, a SysEx (longer than a MidiEvent)
    // included. The plugin stages a block's DAW events, then a moved automation slot's CC at frame 0.
    KernelRig rig({{routing("sendToAll")}, {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
    rig.block({.midi = {{100, on(0, 60)}, {300, on(1, 62)}, {0, cc(0, 7, 100)}}});
    rig.block({.midi = {{5, off(0, 60)}, {5, on(0, 64)}, {5, on(2, 67)}, {1023, off(1, 62)}}});
    rig.block({.midi = {{10, {0xF0, 0x7D, 1, 2, 3, 4, 5, 6, 7, 0xF7}}, {0, {0xF8}}, {10, {0xFA}}}});
    for (std::uint32_t frames : {1u, 1u, 33u, 1u, 222u, 1u, 7u, 4096u, 2u})
        rig.block({.frames = frames, .midi = {{0, on(3, 40 + static_cast<int>(frames % 50))}, {frames - 1, off(3, 40)}}});
    rig.run({.frames = 8192}, 2);  // nothing staged
    // Each routing, a structure change that keeps the node's ports (a swap).
    for (const char* mode : {"oneChannelPerInstance", "midiChannelToInstance", "fourChannelsPerInstance"}) {
        INFO(mode);
        rig.restructure({{routing(mode)}, {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
        for (int ch = 0; ch < 8; ++ch) rig.block({.frames = 512, .midi = {{static_cast<std::uint32_t>(ch * 50), on(ch, 50 + ch)}}});
    }
    CHECK(rig.seen.serialIn > 100);
    CHECK(rig.seen.midi > 30);
    CHECK(rig.raisedAt().empty());
    rig.finish();
}

TEST_CASE("the kernel node forwards a block that fills rp.host.midi-in, all of it, to a Game Boy's serial port and a NES",
          "[engine-graph][kernel-node]") {
    // The one place host MIDI is dropped is rp.host.midi-in's output: every output the kernel forwards it on holds at
    // least as much. So a block that fills it to the message and the byte (two dense curves, a CC and a pitch bend,
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

    KernelRig rig({{routing("sendToAll")}, {gameBoy(1, kMgb), nes(2, {kNesMidi})}});
    rig.block({.frames = kFrames, .midi = midi});
    const std::vector<Message> serialIn = serialBytes(rig.node<Probe>("in-1").bytes);
    const std::vector<Message>& nesMidi = rig.node<Probe>("in-2").events;
    CHECK(rig.host->droppedMessages("midi-in") == 0);
    CHECK(rig.host->droppedMessages("kernel") == 0);
    CHECK(serialIn.size() == bytes.size());
    CHECK(rig.node<Probe>("in-1").bytes.size() == messages.size());  // a run for each message, each at its own frame
    CHECK(nesMidi.size() == messages.size());
    CHECK(text(serialIn) == text(bytes));
    CHECK(text(nesMidi) == text(messages));
    CHECK(rig.raisedAt().empty());
    rig.finish();
}

TEST_CASE("the kernel node drives each LSDj sync mode as today's kernel does, MI.OUT and master sync among them",
          "[engine-graph][kernel-node]") {
    KernelRig rig({{routing("sendToAll")},
                   {gameBoy(1, lsdj("midiSync", 1, true)), gameBoy(2, lsdj("midiSync", 2)), gameBoy(3, lsdj("midiSyncArduinoboy", 1, true)),
                    gameBoy(4, lsdj("midiMap")), gameBoy(5, lsdj("keyboardMidi")), gameBoy(6, lsdj("midiPassthrough")),
                    gameBoy(7, lsdj("midiOut")), gameBoy(8, lsdj("masterSync")), gameBoy(9, lsdj("off"))}});
    // Stopped: notes only (MI.MAP rows, Arduinoboy rows and its play flag, keyboard scancodes, passthrough bytes).
    rig.block({.midi = {{0, on(0, 5)}, {40, on(0, 24)}, {41, on(0, 40)}, {90, on(0, 48)}, {91, on(1, 9)}}});
    rig.block({.midi = {{3, off(0, 5)}, {3, off(1, 9)}, {700, on(0, 72)}, {701, on(0, 27)}}});
    // The transport starts with a row note in the block: the Arduinoboy START and the clocks go out at frame 0 after
    // the row byte (raised to its frame on the wire), and MI.MAP's clocks after its row.
    rig.block({.ppq = 0.0, .playing = true, .midi = {{200, on(0, 31)}, {200, on(0, 7)}}});
    BlockIn playing{.frames = 1024, .ppq = 1024.0 / kBeat, .playing = true};
    rig.run(playing, 30);
    // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run, a quiet block, then a
    // stopped LSDj's idle flood, then a run again.
    Bytes flood;
    for (int i = 0; i < 40; ++i) flood.push_back(static_cast<std::uint8_t>(i & 0x7f));
    playing.ppq = 31 * 1024.0 / kBeat;
    for (auto captured : {std::vector<std::pair<std::uint32_t, Bytes>>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
                                                                       {8, {5, 5, 5}}},
                          {{7, {0x80, framed(0x74), framed(64)}}, {8, {}}},
                          {{8, flood}},
                          {{8, flood}, {7, {framed(0x7e)}}},
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
    CHECK(rig.seen.serialIn > 200);
    CHECK(rig.seen.buttons > 0);  // autoStart's START press and release
    CHECK(rig.seen.midiOut > 10);
    // Arduinoboy's transport bytes and clocks, and MI.MAP's clocks, behind a row byte: on those two alone.
    CHECK(rig.raisedAt() == std::set<std::string>{"sys-3.serialIn", "sys-4.serialIn"});
    rig.finish();
}

TEST_CASE("the kernel node drives risa and smsggdj host sync, and a NES's host MIDI, as today's kernel does",
          "[engine-graph][kernel-node]") {
    KernelRig rig({{routing("sendToAll")}, {nes(1, {kNesMidi, kRisa}), nes(2, {sms("sms")}), nes(3, {sms("gg")})}});
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
    CHECK(rig.seen.bytes > 50);
    CHECK(rig.seen.midi > 0);
    CHECK(rig.raisedAt().empty());
    rig.finish();
}

TEST_CASE("the kernel node runs a controller session as today's kernel does: its surface, its launches, its LEDs",
          "[engine-graph][kernel-node]") {
    // A Launchpad driving an emulated cart in MI.MAP (whose clock and launches go to its serial port), beside a
    // Game Boy decoding MI.OUT, so project-stage and system-stage host MIDI out interleave on one port.
    const auto structure = [](const std::string& target) {
        return Structure{{routing("sendToAll"), launchpad(target, 7, "bar")}, {gameBoy(7, lsdj("midiMap")), gameBoy(8, lsdj("midiOut"))}};
    };
    KernelRig rig(structure("system"));
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
    CHECK(rig.seen.controllerOut > 20);
    CHECK(rig.seen.serialIn > 100);
    CHECK(rig.seen.midiOut > 40);
    CHECK(rig.raisedAt().empty());
    rig.finish();
}

TEST_CASE("the kernel node keeps today's state across structure changes: survivors go on, a removed id is pruned",
          "[engine-graph][kernel-node]") {
    // Every change moves the node's ports, so each is a reshape: the instance is kept (the rig checks) and setSystems
    // runs at the swap. A clock that survives does not restart; an id removed and added again starts afresh.
    const Structure first{{routing("sendToAll")}, {gameBoy(1, lsdj("midiSync")), gameBoy(2, kMgb), gameBoy(3, lsdj("midiSyncArduinoboy"))}};
    KernelRig rig(first);
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
    rig.restructure({{routing("oneChannelPerInstance")},
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
    CHECK(rig.seen.serialIn > 50);
    CHECK(rig.seen.bytes > 10);
    CHECK(rig.raisedAt() == std::set<std::string>{"sys-3.serialIn"});  // Arduinoboy's clocks behind a row byte
    rig.finish();
}

TEST_CASE("a throw in the kernel node's roles keeps what was sent before it and runs the next block, as today",
          "[engine-graph][kernel-node]") {
    // No shipped role throws on what the kernel accepts, so the throw is injected: run after both bundles, in both
    // runtimes, Math.ceil throws for one value, which walkTicks reaches when the second system's clock first syncs,
    // at ppq 10.5 (tick 252). The first system has forwarded its MIDI by then; the third never runs that block.
    const std::string inject = R"JS(
      (function () {
        var ceil = Math.ceil;
        Math.ceil = function (x) { if (x === 252) throw new Error("injected"); return ceil(x); };
      })();
    )JS";
    KernelRig rig({{routing("sendToAll")}, {gameBoy(1, kMgb), gameBoy(2, lsdj("midiSync")), gameBoy(3, kMgb)}}, inject);
    rig.block({.midi = {{0, on(0, 60)}}});
    rig.block({.ppq = 10.5, .playing = true, .midi = {{4, on(0, 62)}}});
    CHECK(rig.scripts.throwCount() == 1);
    rig.run({.ppq = 10.5 + 1024.0 / kBeat, .playing = true, .midi = {{4, on(0, 64)}}}, 3);
    CHECK(rig.seen.serialIn > 0);
    CHECK(rig.raisedAt().empty());
    rig.finish(1);
}

TEST_CASE("the kernel node reshapes under a running audio thread, its instance kept through every structure change",
          "[engine-graph][kernel-node][stress]") {
    // Today's kernel is not run here: this is the threading of the real node, its settings committed and setSystems
    // run on the audio thread at each swap, while the control thread plans the next and reclaims the last.
    const std::vector<Structure> structures{
        {{routing("sendToAll")}, {gameBoy(1, lsdj("midiSync")), nes(2, {kNesMidi, kRisa})}},
        {{routing("sendToAll"), launchpad("system", 1, "bar")}, {gameBoy(1, lsdj("midiMap")), nes(2, {kNesMidi, kRisa}), gameBoy(3, kMgb)}},
        {{routing("oneChannelPerInstance")}, {gameBoy(3, kMgb)}},
        {{routing("sendToAll")}, {}},
    };
    KernelRig rig(structures[0]);
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
    CHECK(rig.host->node("kernel") == rig.kernel);
    CHECK(rig.scripts.liveInstances() == 1);
    rig.finish();
}
