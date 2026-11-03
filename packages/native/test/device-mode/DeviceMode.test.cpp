// The device build of the node SDK (docs/modular-graph-phase-e1-design.md sections 6 and 8), tested on the desktop.
//
// rp.core and rp.dsp are built twice into this binary: by their own targets, as every desktop product has them, and in
// device mode (RP_DEVICE, no exceptions, no RTTI) by retroplug-device-mode-objects, beside the echo node of
// EchoNode.hpp (cmake/tests.cmake says how the two builds share one binary). A device-mode type has no descriptor and
// no layout, since a device never plans, so the planner reaches one here through a twin: a type that answers
// descriptor and layout_for from the desktop's build of the same node and creates its instances with the device's,
// handing the device's create the msgpack the desktop's settings converter (graph/JsonToMsgpack.hpp) writes from the
// document's JSON, as E3's plan export will. The twin's instances run through a real GraphHost.
//
// - Every portable rp.core and rp.dsp type (every one but rp.dsp.resample and rp.dsp.dpcm, which declare heap) over a
//   table of settings, its defaults and each field moved, resources named where the type takes one: a plan whose node
//   is the device's build renders, over ragged blocks and with its params moving, exactly what the same plan renders
//   with the desktop's build, at every probe the plan feeds and in every buffer it records.
// - The device-mode instance's query_memory, create, activate, process, ports_changed, get_interface, deactivate and
//   destroy allocate nothing (the counting allocator, around each call the twin forwards).
// - The decode: a missing field keeps its default, an unknown one is stepped over, every kind of field reads what the
//   desktop's JSON reader reads, and an over-long text or list, a value of the wrong type and a malformed blob are
//   refused, a field's refusal naming it. The desktop's build reads msgpack too, through the same decode: a node made
//   from msgpack is the one made from JSON, and the whole table renders the same with the desktop's types reading
//   msgpack.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/node.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"

// The rig's own nodes' settings, in a named namespace as a desktop Settings struct's schema needs.
namespace rp::graph::devtest_rig {

struct NoiseSettings {
    std::uint32_t channels = 2;
    std::string   layout;  // empty: the count's own name (mono, stereo, discrete(n))
    std::uint32_t seed = 1;
};

}  // namespace rp::graph::devtest_rig

namespace {

using namespace rp::graph;

// ---- the rig's nodes: a deterministic source of every port kind, and a probe that digests what reaches it -------

struct Noise {
    static constexpr const char* id = "rp.devtest.noise";
    using Settings = devtest_rig::NoiseSettings;
    struct {
        rp::audio_out<rp::stereo> out;
        rp::events_out            events;
        rp::control_out           control;
    } outputs;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "noise"); }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels =
            ChannelSpec::fixed(s.channels, s.layout.empty() ? layoutForCount(s.channels) : s.layout);
    }

    std::uint32_t state = 1;
    void on_settings(const Settings& s) noexcept { state = s.seed * 2654435761u + 1u; }

    std::uint32_t next() noexcept {  // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float sample() noexcept { return (static_cast<float>(next() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 1.2f; }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = sample();
        // Up to three messages and two control changes a block, each at a frame of its own, in order.
        std::uint32_t frame = 0;
        for (std::uint32_t k = next() % 4; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 8) {
            const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(next() & 0x7f), 100};
            outputs.events.push(frame, note);
        }
        frame = 0;
        for (std::uint32_t k = next() % 3; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 16)
            outputs.control.push(frame, static_cast<double>(sample()) * 3.0);
    }
};

// FNV-1a 64 over everything a block brings each input: the frame count, each audio channel's samples bit for bit,
// each message's frame, flags and bytes, the control stream's start and changes. Nothing is allocated on the block.
struct Probe {
    static constexpr const char* id = "rp.devtest.probe";
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;

    std::uint64_t hash = 1469598103934665603ull;
    std::uint32_t blocks = 0;
    bool          heard = false;  // anything but silence and empty queues reached it

    void mix(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    }
    template <class V>
    void mix(const V& v) noexcept { mix(&v, sizeof v); }

    void process(const rp::Block& b) noexcept {
        mix(b.frames());
        mix(inputs.in.channel_count);
        for (std::uint32_t c = 0; c < inputs.in.channel_count; ++c) {
            mix(inputs.in[c], b.frames() * sizeof(float));
            for (std::uint32_t i = 0; i < b.frames(); ++i) heard = heard || inputs.in[c][i] != 0.0f;
        }
        for (const auto& m : inputs.events.messages()) {
            mix(m.frame);
            mix(m.flags);
            const auto data = inputs.events.data(m);
            mix(data.data(), data.size());
            heard = true;
        }
        mix(inputs.control.start());
        for (const auto& c : inputs.control.changes()) {
            mix(c.frame);
            mix(c.value);
            heard = true;
        }
        heard = heard || inputs.control.start() != 0.0;
        ++blocks;
    }
};

const rp_package* rigPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Noise>(), rp::node_type<Probe>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.rig","version":"1.0.0"})", 2, kTypes};
    return &kPackage;
}

const rp_package* echoPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<devtest::Echo>(), rp::node_type<devtest::Pair>(),
                                                 rp::node_type<devtest::Shaper>(), rp::node_type<devtest::Bare>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 4, kTypes};
    return &kPackage;
}

// ---- twins: the desktop's description, another build's instances -----------------------------------------------

struct Tally {
    int allocations = 0;
    int frees = 0;
    int calls = 0;
};

struct Twin {
    rp_node_type        type{};
    const rp_node_type* desc = nullptr;  // descriptor and layout_for: the desktop's build
    const rp_node_type* inst = nullptr;  // query_memory, create, destroy: the build under test
    bool                counted = false;  // count the instance type's calls (a device-mode type's)
    Tally               tally;
};

struct LiveNode {
    const rp_node_vtable* vt;  // the instance's own vtable, which the counting one forwards to
    Twin*                 twin;
};
std::map<const rp_node*, LiveNode>& liveNodes() {
    static std::map<const rp_node*, LiveNode> nodes;
    return nodes;
}

// f() with this thread's allocations counted into the twin's tally (the C library's too, where the binary sees them).
template <class F>
auto counted(Twin& twin, F&& f) {
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    struct End {
        Twin& twin;
        ~End() {
            const auto c = rp::test::endCountingAllocations();
            twin.tally.allocations += c.allocations;
            twin.tally.frees += c.frees;
            ++twin.tally.calls;
        }
    } end{twin};
    return f();
}

Twin* twinOf(const rp_node_type* t) { return static_cast<Twin*>(t->ctx); }

// The settings the planner hands a type, as JSON, converted to the msgpack a device reads. A conversion that fails
// is reported through the type's error, so the plan refuses rather than a callback throwing into the host.
bool toMsgpack(const rp_settings* s, std::vector<std::uint8_t>* out, rp_error* err) {
    const std::string_view json = s && s->data && s->size ? std::string_view(s->data, s->size) : std::string_view("{}");
    auto converted = settingsToMsgpack(json);
    if (!converted) {
        rp::detail::copyError(err, converted.error().what());
        return false;
    }
    *out = std::move(*converted);
    return true;
}

const char* twinDescriptor(const rp_node_type* t) { return twinOf(t)->desc->descriptor(twinOf(t)->desc); }

std::size_t twinLayoutFor(const rp_node_type* t, const rp_settings* s, char* out, std::size_t capacity, rp_error* err) {
    return twinOf(t)->desc->layout_for(twinOf(t)->desc, s, out, capacity, err);
}

bool twinQueryMemory(const rp_node_type* t, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    Twin& twin = *twinOf(t);
    std::vector<std::uint8_t> bytes;
    if (!toMsgpack(s, &bytes, err)) return false;
    const rp_settings m{reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
    if (!twin.counted) return twin.inst->query_memory(twin.inst, &m, out, err);
    return counted(twin, [&] { return twin.inst->query_memory(twin.inst, &m, out, err); });
}

bool countedActivate(rp_node* n, double rate, std::uint32_t frames, rp_error* err) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->activate(n, rate, frames, err); });
}
void countedDeactivate(rp_node* n) {
    const LiveNode& l = liveNodes().at(n);
    counted(*l.twin, [&] { l.vt->deactivate(n); });
}
void countedPortsChanged(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
    const LiveNode& l = liveNodes().at(n);
    counted(*l.twin, [&] { l.vt->ports_changed(n, connected, count); });
}
rp_status countedProcess(rp_node* n, const rp_process* p) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->process(n, p); });
}
const void* countedInterface(rp_node* n, const char* id) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->get_interface(n, id); });
}
// A device-mode vtable's settings entries are null, and so are these: a twin is planned once and never re-planned.
const rp_node_vtable kCountingVtable{&countedActivate, &countedDeactivate, &countedPortsChanged, &countedProcess,
                                     nullptr,          nullptr,            nullptr,              &countedInterface};

rp_node* twinCreate(const rp_node_type* t, const rp_host* host, void* memory, const rp_settings* s, rp_error* err) {
    Twin& twin = *twinOf(t);
    std::vector<std::uint8_t> bytes;
    if (!toMsgpack(s, &bytes, err)) return nullptr;
    const rp_settings m{reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
    rp_node* n = twin.counted ? counted(twin, [&] { return twin.inst->create(twin.inst, host, memory, &m, err); })
                              : twin.inst->create(twin.inst, host, memory, &m, err);
    if (!n) return nullptr;
    liveNodes().emplace(n, LiveNode{n->vt, &twin});
    if (twin.counted) n->vt = &kCountingVtable;
    return n;
}

void twinDestroy(rp_node* n) {
    const auto it = liveNodes().find(n);
    REQUIRE(it != liveNodes().end());
    const LiveNode l = it->second;
    liveNodes().erase(it);
    n->vt = l.vt;
    if (l.twin->counted) counted(*l.twin, [&] { l.twin->inst->destroy(n); });
    else l.twin->inst->destroy(n);
}

// A package of twins: every type `instances` has, described by the type of the same id in `desktop`.
struct TwinPackage {
    std::vector<std::unique_ptr<Twin>> twins;
    std::vector<const rp_node_type*>   types;
    rp_package                         package{};

    TwinPackage(const rp_package* desktop, const rp_package* instances, bool count) {
        for (std::uint32_t i = 0; i < instances->node_type_count; ++i) {
            const rp_node_type* inst = instances->node_types[i];
            const rp_node_type* desc = nullptr;
            for (std::uint32_t k = 0; k < desktop->node_type_count; ++k)
                if (std::string_view(desktop->node_types[k]->id) == inst->id) desc = desktop->node_types[k];
            INFO(inst->id);
            REQUIRE(desc);
            REQUIRE(desc->flags == inst->flags);
            auto twin = std::make_unique<Twin>();
            twin->desc = desc;
            twin->inst = inst;
            twin->counted = count;
            twin->type = rp_node_type{inst->id,     &twinDescriptor, &twinLayoutFor, &twinQueryMemory,
                                      &twinCreate,  &twinDestroy,    inst->flags,    twin.get()};
            types.push_back(&twin->type);
            twins.push_back(std::move(twin));
        }
        package = rp_package{RP_ABI_VERSION, instances->manifest_json, static_cast<std::uint32_t>(types.size()),
                             types.data()};
    }
};

// ---- the rig and the table -------------------------------------------------------------------------------------

constexpr std::uint32_t kMaxBlock = 1024;
constexpr std::size_t   kFrames = 4096;
// Ragged blocks, as the DSP nodes' tests split a stream: one frame, a prime, a large one, and so on, cycling.
const std::vector<std::uint32_t> kSplit = {1, 31, 512, 3, 1024, 77};

struct Rig {
    NodeRegistry               registry;
    ResourceTable              resources;
    std::unique_ptr<GraphHost> host;
    std::vector<std::vector<float>> outs = std::vector<std::vector<float>>(2, std::vector<float>(kMaxBlock));
    std::vector<float*>        outPointers{outs[0].data(), outs[1].data()};
    rp_transport               transport{48000.0, 120.0, 0.0, 0};
    ResourceTable::Id          a = 0, b = 0, c = 0;  // a stereo source, a mono source, a stereo record target

    explicit Rig(const std::vector<const rp_package*>& packages) {
        std::string why;
        for (const rp_package* p : packages) {
            const bool added = registry.addPackage(p, &why);
            INFO(why);
            REQUIRE(added);
        }
        host = std::make_unique<GraphHost>(registry, resources, HostConfig{48000.0, kMaxBlock, Target::Desktop});
        a = buffer(2, 3000, 11);
        b = buffer(1, 2000, 23);
        c = resources.createAudioBuffer(2, 4096, 48000.0);
    }

    ResourceTable::Id buffer(std::uint32_t channels, std::uint32_t frames, std::uint32_t seed) {
        const auto id = resources.createAudioBuffer(channels, frames, 48000.0);
        auto* buf = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        std::uint32_t x = seed;
        for (std::uint32_t ch = 0; ch < channels; ++ch)
            for (std::uint32_t i = 0; i < frames; ++i) {
                x = x * 1664525u + 1013904223u;
                buf->data[ch][i] = (static_cast<float>(x >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.4f;
            }
        return id;
    }

    // `document` with $A, $B and $C replaced by this rig's resource ids, as "res:N".
    std::string resolve(std::string document) const {
        for (const auto& [mark, id] : {std::pair{std::string("$A"), a}, {std::string("$B"), b}, {std::string("$C"), c}})
            for (std::size_t at; (at = document.find(mark)) != std::string::npos;)
                document.replace(at, mark.size(), "res:" + std::to_string(id));
        return document;
    }
};

// One param change, at the start of the block with this index.
struct Move {
    std::size_t block;
    std::string node;
    std::string param;
    double      value;
};

struct Case {
    std::string              name;
    std::string              document;
    std::vector<std::string> probes;
    std::vector<Move>        moves;
    bool                     records = false;  // the plan records into $C

    Case(std::string n, std::string d, std::vector<std::string> p, std::vector<Move> m = {}, bool r = false)
        : name(std::move(n)), document(std::move(d)), probes(std::move(p)), moves(std::move(m)), records(r) {}
};

std::string noise(const std::string& id, std::uint32_t channels, std::uint32_t seed, const std::string& layout = "") {
    return R"({"id":")" + id + R"(","type":"rp.devtest.noise","settings":{"channels":)" + std::to_string(channels) +
           R"(,"seed":)" + std::to_string(seed) + R"(,"layout":")" + layout + R"("}})";
}
std::string probe(const std::string& id) { return R"({"id":")" + id + R"(","type":"rp.devtest.probe"})"; }
std::string node(const std::string& type, const std::string& settings, const std::string& params = "{}",
                 const std::string& resources = "{}") {
    return R"({"id":"x","type":")" + type + R"(","settings":)" + settings + R"(,"params":)" + params +
           R"(,"resources":)" + resources + "}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + R"("})";
}
std::string doc(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string out = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) out += (i ? "," : "") + nodes[i];
    out += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) out += (i ? "," : "") + wires[i];
    return out + "]}";
}

// A node with one audio input and output, fed by noise and probed.
Case through(const std::string& name, const std::string& type, const std::string& settings, std::uint32_t channels,
             const std::string& layout = "", const std::string& params = "{}", std::vector<Move> moves = {}) {
    return {name,
            doc({noise("n", channels, 7, layout), node(type, settings, params), probe("p")},
                {wire("n.out", "x.in"), wire("x.out", "p.in")}),
            {"p"},
            std::move(moves)};
}

std::vector<Case> cases() {
    std::vector<Case> t;
    // rp.core.add, audio and control
    t.push_back({"add, defaults",
                 doc({noise("n1", 2, 1), noise("n2", 2, 2), node("rp.core.add", "{}"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"add, three inputs",
                 doc({noise("n1", 1, 1), noise("n2", 1, 2), noise("n3", 1, 3), node("rp.core.add", R"({"inputs":3})"),
                      probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("n3.out", "x.in3"),
                      wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"add, control",
                 doc({noise("n1", 1, 4), noise("n2", 1, 5), node("rp.core.add", R"({"kind":"control"})"), probe("p")},
                     {wire("n1.control", "x.in1"), wire("n2.control", "x.in2"), wire("x.out", "p.control")}),
                 {"p"}});
    // rp.core.merge
    t.push_back({"merge, defaults",
                 doc({noise("n1", 1, 6), noise("n2", 1, 7), node("rp.core.merge", "{}"), probe("p")},
                     {wire("n1.events", "x.in1"), wire("n2.events", "x.in2"), wire("x.out", "p.events")}),
                 {"p"}});
    t.push_back({"merge, three inputs",
                 doc({noise("n1", 1, 6), noise("n2", 1, 7), noise("n3", 1, 8), node("rp.core.merge", R"({"inputs":3})"),
                      probe("p")},
                     {wire("n1.events", "x.in1"), wire("n2.events", "x.in2"), wire("n3.events", "x.in3"),
                      wire("x.out", "p.events")}),
                 {"p"}});
    // rp.core.join and rp.core.split
    t.push_back({"join, defaults",
                 doc({noise("n1", 1, 9), noise("n2", 1, 10), node("rp.core.join", "{}"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"join, inputs and layout moved",
                 doc({noise("n1", 2, 9), noise("n2", 1, 10),
                      node("rp.core.join", R"j({"inputs":[2,1],"layout":"discrete(3)"})j"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"split, defaults",
                 doc({noise("n", 2, 11), node("rp.core.split", "{}"), probe("p1"), probe("p2")},
                     {wire("n.out", "x.in"), wire("x.out1", "p1.in"), wire("x.out2", "p2.in")}),
                 {"p1", "p2"}});
    t.push_back({"split, outputs and layout moved",
                 doc({noise("n", 3, 11), node("rp.core.split", R"j({"outputs":[1,2],"layout":"discrete(3)"})j"),
                      probe("p1"), probe("p2")},
                     {wire("n.out", "x.in"), wire("x.out1", "p1.in"), wire("x.out2", "p2.in")}),
                 {"p1", "p2"}});
    // rp.core.downmix and rp.core.relabel
    t.push_back(through("downmix, defaults", "rp.core.downmix", "{}", 2));
    t.push_back(through("downmix, 5.1 to stereo", "rp.core.downmix", R"({"from":"5.1","to":"stereo"})", 6, "5.1"));
    t.push_back(through("downmix, 5.1 to mono", "rp.core.downmix", R"({"from":"5.1"})", 6, "5.1"));
    t.push_back(through("relabel, defaults", "rp.core.relabel", "{}", 6));
    t.push_back(through("relabel, channels and layout moved", "rp.core.relabel",
                        R"({"channels":2,"layout":"stereo"})", 2));
    // rp.core.delay
    t.push_back(through("delay, defaults", "rp.core.delay", "{}", 2, "", R"({"time":100})",
                        {{3, "x", "time", 2000}, {7, "x", "time", 1}}));
    t.push_back(through("delay, maxSamples and channels moved", "rp.core.delay",
                        R"({"maxSamples":100,"channels":1})", 1, "", R"({"time":37})",
                        {{2, "x", "time", 250}, {5, "x", "time", 12}}));
    // rp.core.constant
    t.push_back({"constant, defaults",
                 doc({node("rp.core.constant", "{}", R"({"value":0.3})"), probe("p")}, {wire("x.out", "p.control")}),
                 {"p"},
                 {{2, "x", "value", 0.9}, {4, "x", "value", 0.1}}});
    t.push_back({"constant, min and max moved",
                 doc({node("rp.core.constant", R"({"min":-2,"max":5})", R"({"value":4})"), probe("p")},
                     {wire("x.out", "p.control")}),
                 {"p"},
                 {{3, "x", "value", -1.5}}});
    // rp.core.buffer-player and rp.core.buffer-recorder
    t.push_back({"buffer-player, defaults",
                 doc({node("rp.core.buffer-player", "{}", "{}", R"({"sample":"$A"})"), probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"buffer-player, channels, layout and loop moved",
                 doc({node("rp.core.buffer-player", R"({"channels":1,"layout":"mono","loop":false})", "{}",
                           R"({"sample":"$B"})"),
                      probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"buffer-recorder, defaults",
                 doc({noise("n", 2, 12), node("rp.core.buffer-recorder", "{}", "{}", R"({"target":"$C"})")},
                     {wire("n.out", "x.in")}),
                 {},
                 {{3, "x", "record", 0}, {6, "x", "record", 1}},
                 true});
    t.push_back({"buffer-recorder, channels moved",
                 doc({noise("n", 1, 13), node("rp.core.buffer-recorder", R"({"channels":1})", "{}",
                                               R"({"target":"$C"})")},
                     {wire("n.out", "x.in")}),
                 {},
                 {{2, "x", "record", 0}, {4, "x", "record", 1}},
                 true});
    // rp.dsp.gain, rp.dsp.biquad, rp.dsp.dither
    t.push_back(through("gain, defaults", "rp.dsp.gain", "{}", 2, "", R"({"gain":0.37})",
                        {{2, "x", "gain", -1.5}, {5, "x", "gain", 0.37}}));
    t.push_back(through("gain, channels moved", "rp.dsp.gain", R"({"channels":1})", 1, "", R"({"gain":2.5})"));
    t.push_back(through("biquad, defaults", "rp.dsp.biquad", "{}", 2, "", R"({"frequency":2000,"q":0.7})",
                        {{3, "x", "frequency", 300}, {6, "x", "q", 4}}));
    t.push_back(through("biquad, type and channels moved", "rp.dsp.biquad", R"({"type":"peak","channels":1})", 1, "",
                        R"({"frequency":900,"q":2,"gainDb":6})", {{4, "x", "gainDb", -9}}));
    t.push_back(through("biquad, high-shelf", "rp.dsp.biquad", R"({"type":"high-shelf"})", 2, "",
                        R"({"frequency":5000,"gainDb":-4})"));
    t.push_back(through("dither, defaults", "rp.dsp.dither", "{}", 2));
    t.push_back(through("dither, algorithm, bits and seed moved", "rp.dsp.dither",
                        R"({"algorithm":"high-pass-tpdf","bits":6,"seed":99})", 2));
    t.push_back(through("dither, jjn and channels moved", "rp.dsp.dither", R"({"algorithm":"jjn","channels":1})", 1));
    t.push_back(
        through("dither, seedFrom named", "rp.dsp.dither", R"({"algorithm":"shaped-tpdf","seedFrom":"$B"})", 2));
    // rp.devtest.shaper: an enum settings field and an enum_param, which a device reads with enums.hpp
    t.push_back(through("shaper, defaults", "rp.devtest.shaper", "{}", 2, "", "{}",
                        {{2, "x", "shape", 0}, {4, "x", "shape", 2}, {6, "x", "shape", 1.4}, {8, "x", "shape", 9}}));
    t.push_back(through("shaper, curve and channels moved", "rp.devtest.shaper", R"({"curve":"Fold","channels":1})",
                        1, "", R"({"shape":"invert"})", {{3, "x", "shape", -1}}));
    t.push_back(through("shaper, curve as a number", "rp.devtest.shaper", R"({"curve":"1"})", 2));
    // rp.dsp.normalize, which names its buffer in its settings
    t.push_back({"normalize, buffer named",
                 doc({node("rp.dsp.normalize", R"({"buffer":"$A"})"), probe("p")}, {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"normalize, gain and channels moved",
                 doc({node("rp.dsp.normalize", R"({"buffer":"$B","gain":0.5,"channels":1})"), probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    return t;
}

struct Run {
    std::vector<std::uint64_t> hashes;
    std::vector<bool>          heard;
    std::vector<float>         recorded;
};

Run render(Rig& rig, const Case& c) {
    const auto result = rig.host->setGraph(rig.resolve(c.document));
    std::string errors;
    for (const auto& e : result.errors) errors += e.code + ": " + e.message + "\n";
    INFO(c.name << "\n" << errors);
    REQUIRE(result.ok);
    std::size_t at = 0, block = 0;
    while (at < kFrames) {
        for (const auto& m : c.moves)
            if (m.block == block) REQUIRE(rig.host->setParam(m.node, m.param, m.value));
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block % kSplit.size()], kFrames - at));
        rig.host->processBlock(n, rig.transport, rig.outPointers.data(), 2);
        at += n;
        ++block;
    }
    Run run;
    for (const auto& id : c.probes) {
        const Probe* p = rp::peer_as<Probe>(rig.host->node(id));
        REQUIRE(p);
        REQUIRE(p->blocks == block);
        run.hashes.push_back(p->hash);
        run.heard.push_back(p->heard);
    }
    if (c.records) {
        const auto* buf = static_cast<const rp_audio_buffer*>(rig.resources.get(rig.c)->payload);
        for (std::uint32_t ch = 0; ch < buf->channels; ++ch)
            run.recorded.insert(run.recorded.end(), buf->data[ch], buf->data[ch] + buf->frames);
    }
    return run;
}

bool anyNonZero(const std::vector<float>& v) {
    for (const float x : v)
        if (x != 0.0f) return true;
    return false;
}

// Every case rendered by `twins` and by the desktop's own types, compared bit for bit.
void compareTable(const TwinPackage& core, const TwinPackage& dsp, const TwinPackage& devtest) {
    for (const Case& c : cases()) {
        INFO(c.name);
        Rig desktop({rp_core_package(), rp_dsp_package(), echoPackageDesktop(), hostPackage(), rigPackage()});
        Rig twin({&core.package, &dsp.package, &devtest.package, hostPackage(), rigPackage()});
        const Run want = render(desktop, c);
        const Run got = render(twin, c);
        for (std::size_t i = 0; i < want.hashes.size(); ++i) {
            INFO("probe " << c.probes[i]);
            CHECK(want.heard[i]);  // the comparison is of a stream, not of two silences
            CHECK(got.hashes[i] == want.hashes[i]);
        }
        if (c.records) {
            CHECK(anyNonZero(want.recorded));
            CHECK(got.recorded == want.recorded);
        }
    }
}

// ---- direct calls: one instance of a type, from settings in either encoding -------------------------------------

std::vector<std::uint8_t> msgpackOf(const std::string& json) {
    auto r = settingsToMsgpack(json);
    INFO(json);
    REQUIRE(r);
    return *r;
}

rp_settings asSettings(const std::vector<std::uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
}
rp_settings asSettings(const std::string& json) { return {json.data(), json.size(), RP_SETTINGS_JSON}; }

// One instance in memory of its own, destroyed with this; `refusal` is what query_memory or create refused it with.
struct Made {
    const rp_node_type*            type;
    std::vector<std::max_align_t>  memory;
    rp_node*                       node = nullptr;
    std::string                    refusal;

    Made(const rp_node_type* t, const rp_settings& s, const rp_host* host = nullptr) : type(t) {
        rp_memory_req req{};
        rp_error      err{};
        if (!t->query_memory(t, &s, &req, &err)) {
            refusal = err.message;
            return;
        }
        REQUIRE(req.align <= alignof(std::max_align_t));
        memory.resize(req.size / sizeof(std::max_align_t) + 1);
        node = t->create(t, host, memory.data(), &s, &err);
        if (!node) refusal = err.message;
    }
    ~Made() {
        if (node) type->destroy(node);
    }
    Made(const Made&) = delete;
    Made& operator=(const Made&) = delete;

    const EchoView& echo() const {
        REQUIRE(node);
        const auto* view = static_cast<const EchoView*>(node->vt->get_interface(node, kEchoInterface));
        REQUIRE(view);
        return *view;
    }
};

const rp_node_type* deviceEcho() { return rp_devtest_package_device_mode()->node_types[0]; }
const rp_node_type* desktopEcho() { return echoPackageDesktop()->node_types[0]; }

void checkSame(const EchoView& a, const EchoView& b) {
    CHECK(a.flag == b.flag);
    CHECK(a.u8 == b.u8);
    CHECK(a.u32 == b.u32);
    CHECK(a.i32 == b.i32);
    CHECK(a.i64 == b.i64);
    CHECK(a.f == b.f);
    CHECK(a.d == b.d);
    CHECK(a.pick == b.pick);
    CHECK(std::string(a.name, a.nameSize) == std::string(b.name, b.nameSize));
    CHECK(std::vector<std::uint16_t>(a.counts, a.counts + a.countsSize) ==
          std::vector<std::uint16_t>(b.counts, b.counts + b.countsSize));
    CHECK(a.rom == b.rom);
    CHECK(a.romResolved == b.romResolved);
    CHECK(a.mode == b.mode);
}

// The device's echo made from `json` as msgpack is the desktop's made from `json` as JSON, field for field.
void checkEcho(const std::string& json) {
    INFO(json);
    const auto bytes = msgpackOf(json);
    Made device(deviceEcho(), asSettings(bytes));
    Made desktop(desktopEcho(), asSettings(json));
    INFO(device.refusal << " / " << desktop.refusal);
    REQUIRE(device.node);
    REQUIRE(desktop.node);
    checkSame(device.echo(), desktop.echo());
}

// What the device's echo refuses `bytes` with; the desktop's build reads msgpack through the same decode, so it must
// refuse them in the same words.
std::string refusal(const std::vector<std::uint8_t>& bytes) {
    Made device(deviceEcho(), asSettings(bytes));
    Made desktop(desktopEcho(), asSettings(bytes));
    CHECK_FALSE(device.node);
    CHECK_FALSE(desktop.node);
    CHECK(device.refusal == desktop.refusal);
    return device.refusal;
}
std::string refusal(const std::string& json) { return refusal(msgpackOf(json)); }

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A host whose resource table holds one resource, a "rom" at id 1, and counts the pins it hands out and takes back.
struct OneRomHost {
    rp_bytes          bytes{nullptr, 0};
    rp_resource       rom{"rom", &bytes};
    int               acquired = 0;
    int               released = 0;
    rp_host_resources service{this, &acquire, &release};
    rp_host           host{RP_ABI_VERSION, this, nullptr, nullptr, nullptr, &interface};

    static const rp_resource* acquire(void* ctx, std::uint32_t id, const char* type, rp_error* err) {
        auto* self = static_cast<OneRomHost*>(ctx);
        if (id != 1 || std::string_view(type) != "rom") {
            rp::detail::copyError(err, "is not a live resource");
            return nullptr;
        }
        ++self->acquired;
        return &self->rom;
    }
    static bool release(void* ctx, std::uint32_t) {
        ++static_cast<OneRomHost*>(ctx)->released;
        return true;
    }
    static const void* interface(const rp_host* h, const char* id) {
        return std::string_view(id) == RP_HOST_RESOURCES_INTERFACE ? &static_cast<OneRomHost*>(h->ctx)->service
                                                                    : nullptr;
    }
};

}  // namespace

TEST_CASE("a device-mode type carries no description and never re-plans", "[device-mode]") {
    for (const rp_package* p : {rp_core_package_device_mode(), rp_dsp_package_device_mode()}) {
        for (std::uint32_t i = 0; i < p->node_type_count; ++i) {
            const rp_node_type* t = p->node_types[i];
            INFO(t->id);
            CHECK(t->descriptor == nullptr);
            CHECK(t->layout_for == nullptr);
            CHECK(t->query_memory != nullptr);
            CHECK(t->create != nullptr);
            CHECK(t->destroy != nullptr);
        }
    }
    // The device's rp.dsp has no rp.dsp.dpcm, which declares heap; every other type is there.
    CHECK(rp_core_package_device_mode()->node_type_count == rp_core_package()->node_type_count);
    CHECK(rp_dsp_package_device_mode()->node_type_count == rp_dsp_package()->node_type_count - 1);
    for (std::uint32_t i = 0; i < rp_dsp_package_device_mode()->node_type_count; ++i)
        CHECK(std::string_view(rp_dsp_package_device_mode()->node_types[i]->id) != "rp.dsp.dpcm");

    // An instance's vtable: the block path and the lifecycle, and no settings swap.
    Made echo(deviceEcho(), asSettings(std::vector<std::uint8_t>{}));
    REQUIRE(echo.node);
    CHECK(echo.node->vt->prepare_settings == nullptr);
    CHECK(echo.node->vt->commit_settings == nullptr);
    CHECK(echo.node->vt->release_settings == nullptr);
    CHECK(echo.node->vt->process != nullptr);

    // And a planning host refuses such a type rather than calling a null descriptor.
    NodeRegistry registry;
    std::string  why;
    CHECK_FALSE(registry.addPackage(rp_devtest_package_device_mode(), &why));
    CHECK(contains(why, "descriptor"));
}

TEST_CASE("every portable rp.core and rp.dsp type renders, built for the device, what the desktop's build renders",
          "[device-mode]") {
    TwinPackage core(rp_core_package(), rp_core_package_device_mode(), true);
    TwinPackage dsp(rp_dsp_package(), rp_dsp_package_device_mode(), true);
    TwinPackage devtest(echoPackageDesktop(), rp_devtest_package_device_mode(), true);
    compareTable(core, dsp, devtest);

    // Every device-mode type the table places ran (the echo, pair and bare nodes are the direct cases' below), and not
    // one of its calls allocated or freed.
    for (const auto* package : {&core, &dsp, &devtest}) {
        for (const auto& twin : package->twins) {
            INFO(twin->type.id);
            const std::string_view id = twin->type.id;
            if (id != "rp.devtest.echo" && id != "rp.devtest.pair" && id != "rp.devtest.bare")
                CHECK(twin->tally.calls > 0);
            CHECK(twin->tally.allocations == 0);
            CHECK(twin->tally.frees == 0);
        }
    }
}

TEST_CASE("the desktop's build reads msgpack settings, and renders the table as it does from JSON", "[device-mode]") {
    TwinPackage core(rp_core_package(), rp_core_package(), false);
    TwinPackage dsp(rp_dsp_package(), rp_dsp_package(), false);
    TwinPackage devtest(echoPackageDesktop(), echoPackageDesktop(), false);
    compareTable(core, dsp, devtest);

    // A node made from msgpack is the one made from JSON, field for field.
    const std::string json =
        R"({"flag":true,"u8":200,"u32":4000000000,"i32":-77,"i64":-5000000000,"f":1.5,"d":-0.125,"pick":"three",)"
        R"("name":"abcdefgh","counts":[9,8,7,6]})";
    const auto bytes = msgpackOf(json);
    Made fromMsgpack(desktopEcho(), asSettings(bytes));
    Made fromJson(desktopEcho(), asSettings(json));
    REQUIRE(fromMsgpack.node);
    REQUIRE(fromJson.node);
    checkSame(fromMsgpack.echo(), fromJson.echo());
    CHECK(fromMsgpack.echo().pick == 2);
    CHECK(fromMsgpack.echo().countsSize == 4);

    // A desktop type whose settings hold desktop types reads JSON alone, and says so.
    const rp_node_type* audioOut = hostPackage()->node_types[0];
    REQUIRE(std::string_view(audioOut->id) == "rp.host.audio-out");
    const auto buses = msgpackOf(R"({"buses":[2]})");
    const rp_settings s = asSettings(buses);
    rp_memory_req req{};
    rp_error      err{};
    CHECK_FALSE(audioOut->query_memory(audioOut, &s, &req, &err));
    CHECK(contains(err.message, "reads JSON alone"));
}

TEST_CASE("the msgpack decode keeps a missing field's default and steps over an unknown one", "[device-mode]") {
    checkEcho("{}");
    checkEcho(R"({"u32":9})");
    checkEcho(R"({"name":"","counts":[]})");
    checkEcho(R"({"flag":true,"u8":255,"u32":4294967295,"i32":-2147483648,"i64":-9223372036854775807,"f":-3,"d":1e300,)"
              R"("pick":"two","name":"12345678","counts":[65535,0,1,2]})");
    // Unknown keys of every msgpack kind, nested ones among them, cost nothing but the step over them.
    checkEcho(R"({"later":{"nested":[1,{"deep":[null,true,"x",2.5]}]},"u8":7,"another":null,"x":[[[]]],"f":2})");
    // An enum by its enumerator's name, or by a number in the string as std::stoi reads one, as reflect-cpp reads it.
    checkEcho(R"({"mode":"Fast"})");
    checkEcho(R"({"mode":"Off"})");
    checkEcho(R"({"mode":"7"})");
    checkEcho(R"({"mode":" 2 and more"})");
    checkEcho(R"({"mode":"+1"})");
    // A number that is no enumerator is read as that number, as reflect-cpp's std::stoi reading takes it: leading white
    // space and a sign skipped, the digits up to the first that is not one.
    CHECK(Made(deviceEcho(), asSettings(msgpackOf(R"({"mode":" 2 and more"})"))).echo().mode == 2);
    CHECK(Made(deviceEcho(), asSettings(msgpackOf(R"({"mode":"+1"})"))).echo().mode == 1);

    // Empty settings are the empty map; a blob with fields in any order is read by name.
    Made empty(deviceEcho(), rp_settings{nullptr, 0, RP_SETTINGS_MSGPACK});
    Made defaults(desktopEcho(), asSettings(std::string("{}")));
    REQUIRE(empty.node);
    checkSame(empty.echo(), defaults.echo());
    checkEcho(R"({"counts":[3],"name":"zz","u8":3})");

    // A field the map names twice keeps the last, as JSON.parse does.
    Made twice(deviceEcho(), asSettings(msgpackOf(R"({"u8":1,"u8":2})")));
    REQUIRE(twice.node);
    CHECK(twice.echo().u8 == 2);

    // Kinds the converter never writes still read: an int stored in a signed form, a float32, and a bin to step over.
    const std::vector<std::uint8_t> handWritten = {0x83,                                   // a map of three
                                                   0xa3, 'u', '3', '2', 0xd0, 0x05,        // "u32": int8 5
                                                   0xa1, 'f', 0xca, 0x3f, 0xc0, 0x00, 0x00,  // "f": float32 1.5
                                                   0xa1, 'z', 0xc4, 0x02, 0xaa, 0xbb};     // "z": bin of 2
    Made hand(deviceEcho(), asSettings(handWritten));
    REQUIRE(hand.node);
    CHECK(hand.echo().u32 == 5);
    CHECK(hand.echo().f == 1.5f);

    // Unknown keys whose values are a bin 16, an ext 8 and a fixext 4 are stepped over too.
    const std::vector<std::uint8_t> exotic = {0x84,                                          // a map of four
                                              0xa1, 'a', 0xc5, 0x00, 0x02, 0x01, 0x02,       // bin 16 of 2
                                              0xa1, 'b', 0xc7, 0x03, 0x05, 0x01, 0x02, 0x03,  // ext 8: 3 bytes, type 5
                                              0xa1, 'c', 0xd6, 0x07, 0x01, 0x02, 0x03, 0x04,  // fixext 4, type 7
                                              0xa2, 'u', '8', 0x09};                         // "u8": 9
    Made skipped(deviceEcho(), asSettings(exotic));
    REQUIRE(skipped.node);
    CHECK(skipped.echo().u8 == 9);
}

TEST_CASE("the msgpack decode refuses what JSON refuses, naming the field", "[device-mode]") {
    // Over a bound: the text's bytes and the list's elements, as the JSON reader words them.
    CHECK(refusal(R"({"name":"123456789"})") ==
          "Failed to parse field 'name': a string of 9 bytes is over the 8 the field holds");
    CHECK(refusal(R"({"counts":[1,2,3,4,5]})") ==
          "Failed to parse field 'counts': an array of 5 elements is over the 4 the field holds");
    // The JSON reader refuses the same settings in the same words; a list element's reason is the one place the
    // decode words its own ("element 1: ...", where reflect-cpp's vector reader has its own phrasing).
    for (const char* json : {R"({"name":"123456789"})", R"({"counts":[1,2,3,4,5]})", R"({"u32":-1})",
                             R"({"u32":2.0})", R"({"u8":256})", R"({"flag":1})", R"({"flag":null})",
                             R"({"pick":"four"})", R"({"rom":"res:007"})", R"({"mode":"Medium"})", R"({"mode":3})",
                             R"({"i32":"1"})", R"({"f":"1"})", R"({"name":5})", R"({"counts":{"a":1}})"}) {
        INFO(json);
        Made desktop(desktopEcho(), asSettings(std::string(json)));
        CHECK_FALSE(desktop.node);
        CHECK(refusal(json) == desktop.refusal);
    }
    {
        Made desktop(desktopEcho(), asSettings(std::string(R"({"counts":[1,"x"]})")));
        CHECK_FALSE(desktop.node);
        CHECK(contains(desktop.refusal, "'counts'"));
        CHECK(refusal(R"({"counts":[1,"x"]})") == "Failed to parse field 'counts': element 1: Could not cast to uint.");
    }

    // A value of the wrong kind or out of its field's range.
    CHECK(refusal(R"({"u32":-1})") == "Failed to parse field 'u32': Could not cast to uint.");
    CHECK(refusal(R"({"u32":2.0})") == "Failed to parse field 'u32': Could not cast to uint.");
    CHECK(refusal(R"({"u8":256})") == "Failed to parse field 'u8': Integer is out of range for the target type.");
    CHECK(refusal(R"({"i32":2147483648})") ==
          "Failed to parse field 'i32': Integer is out of range for the target type.");
    CHECK(refusal(R"({"i32":"1"})") == "Failed to parse field 'i32': Could not cast to int.");
    CHECK(refusal(R"({"f":"1"})") == "Failed to parse field 'f': Could not cast to double.");
    CHECK(refusal(R"({"flag":1})") == "Failed to parse field 'flag': Could not cast to boolean.");
    CHECK(refusal(R"({"flag":null})") == "Failed to parse field 'flag': Could not cast to boolean.");
    CHECK(refusal(R"({"name":5})") == "Failed to parse field 'name': Could not cast to string.");
    CHECK(refusal(R"({"pick":"four"})") == "Failed to parse field 'pick': Literal does not support string 'four'. The "
                                             "following strings are supported: 'one', 'two', 'three'.");
    CHECK(refusal(R"({"mode":"Medium"})") ==
          "Failed to parse field 'mode': Invalid enum value: 'Medium'. Must be one of [Off, Slow, Fast].");
    CHECK(refusal(R"({"mode":"99999999999"})") ==
          "Failed to parse field 'mode': Invalid enum value: '99999999999'. Must be one of [Off, Slow, Fast].");
    CHECK(refusal(R"({"counts":[1,-2]})") == "Failed to parse field 'counts': element 1: Could not cast to uint.");
    CHECK(refusal(R"({"counts":{"a":1}})") == "Failed to parse field 'counts': Could not cast to array!");
    CHECK(refusal(R"({"rom":"res:007"})") ==
          "Failed to parse field 'rom': \"res:007\" is not a resource reference (res:N)");
    CHECK(refusal(R"({"rom":7})") == "Failed to parse field 'rom': Could not cast to string.");

    // The two inputs where the readers part, by choice (settings_msgpack.hpp). An integer above INT64_MAX in a signed
    // field, which reflect-cpp's JSON reader wraps, the decode refuses as out of range.
    {
        Made desktop(desktopEcho(), asSettings(std::string(R"({"i32":18446744073709551615})")));
        REQUIRE(desktop.node);
        CHECK(desktop.echo().i32 == -1);
        CHECK(refusal(R"({"i32":18446744073709551615})") ==
              "Failed to parse field 'i32': Integer is out of range for the target type.");
    }
    {
        // 2^63, a double canonical JSON spells in integer digits.
        Made desktop(desktopEcho(), asSettings(std::string(R"({"i64":9223372036854775808})")));
        REQUIRE(desktop.node);
        CHECK(desktop.echo().i64 == std::numeric_limits<std::int64_t>::min());
        CHECK(refusal(R"({"i64":9223372036854775808})") ==
              "Failed to parse field 'i64': Integer is out of range for the target type.");
    }
    {
        // And -0, an integer literal, is msgpack's 0, which an unsigned field takes; the JSON reader refuses it.
        Made desktop(desktopEcho(), asSettings(std::string(R"({"u32":-0})")));
        CHECK(desktop.refusal == "Failed to parse field 'u32': Could not cast to uint.");
        Made device(deviceEcho(), asSettings(msgpackOf(R"({"u32":-0})")));
        REQUIRE(device.node);
        CHECK(device.echo().u32 == 0);
    }

    // A blob that is not one well-formed map is refused whole.
    CHECK(refusal(msgpackOf("[1,2]")) == "settings must be a msgpack map");
    CHECK(refusal(msgpackOf("7")) == "settings must be a msgpack map");
    auto truncated = msgpackOf(R"({"name":"abc"})");
    truncated.pop_back();
    CHECK(refusal(truncated) == "settings are not valid msgpack");
    auto trailing = msgpackOf(R"({"u8":1})");
    trailing.push_back(0xc0);
    CHECK(refusal(trailing) == "settings hold bytes after their map");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0x01, 0x02}) == "a settings key must be a string");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xc1}) == "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0xdf, 0xff, 0xff, 0xff, 0xff}) == "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xdb, 0xff, 0xff, 0xff, 0xff}) == "settings are not valid msgpack");
    // A value cut short in each wide form: an int16, a float64, a str8.
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa3, 'u', '3', '2', 0xcd, 0x01}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'd', 0xcb, 0x3f, 0xf0, 0x00}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa4, 'n', 'a', 'm', 'e', 0xd9, 0x05, 'a', 'b'}) ==
          "settings are not valid msgpack");
    // And an unknown key's value cut short: a bin 16 and a fixext 8.
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xc5, 0x00, 0x04, 0x01}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xd7, 0x01, 0x02}) == "settings are not valid msgpack");

    // A type with no Settings takes the empty blob and any map, whose keys it ignores (as its JSON side takes any
    // object), and refuses anything else in the words the decode of a Settings uses, on both builds.
    const rp_node_type* const deviceBare = rp_devtest_package_device_mode()->node_types[3];
    const rp_node_type* const desktopBare = echoPackageDesktop()->node_types[3];
    for (const rp_node_type* bare : {deviceBare, desktopBare}) {
        INFO((bare == desktopBare ? "desktop" : "device"));
        REQUIRE(std::string_view(bare->id) == "rp.devtest.bare");
        for (const auto& bytes :
             {std::vector<std::uint8_t>{}, msgpackOf("{}"), msgpackOf(R"({"x":1,"y":[{"z":null}]})")}) {
            Made made(bare, asSettings(bytes));
            CHECK(made.node);
        }
        CHECK(Made(bare, asSettings(msgpackOf("[1,2]"))).refusal == "settings must be a msgpack map");
        CHECK(Made(bare, asSettings(msgpackOf("7"))).refusal == "settings must be a msgpack map");
        auto trailing = msgpackOf("{}");
        trailing.push_back(0xc0);
        CHECK(Made(bare, asSettings(trailing)).refusal == "settings hold bytes after their map");
        CHECK(Made(bare, asSettings(std::vector<std::uint8_t>{0x81, 0xa1, 'x'})).refusal ==
              "settings are not valid msgpack");
    }

    // A device reads msgpack alone.
    Made json(deviceEcho(), asSettings(std::string("{}")));
    CHECK_FALSE(json.node);
    CHECK(json.refusal == "settings encoding 0 is not supported; a device node reads msgpack");

    // A create whose second resource will not pin releases the first, which it had pinned, and names the field that
    // failed, on both builds.
    const rp_node_type* const devicePair = rp_devtest_package_device_mode()->node_types[1];
    const rp_node_type* const desktopPair = echoPackageDesktop()->node_types[1];
    for (const rp_node_type* pair : {devicePair, desktopPair}) {
        INFO((pair == desktopPair ? "desktop" : "device"));
        REQUIRE(std::string_view(pair->id) == "rp.devtest.pair");
        OneRomHost        host;
        const std::string json = R"({"rom":"res:1","sram":"res:2"})";
        const auto        bytes = msgpackOf(json);
        Made              made(pair, pair == desktopPair ? asSettings(json) : asSettings(bytes), &host.host);
        CHECK_FALSE(made.node);
        CHECK(made.refusal == "sram: \"res:2\" is not a live resource");
        CHECK(host.acquired == 1);
        CHECK(host.released == 1);
    }
    {
        // And one that pins both holds them until it is destroyed.
        OneRomHost host;
        {
            Made made(devicePair, asSettings(msgpackOf(R"({"rom":"res:1"})")), &host.host);
            REQUIRE(made.node);
            CHECK(host.acquired == 1);
            CHECK(host.released == 0);
        }
        CHECK(host.released == 1);
    }

    // A resource the settings name is pinned at create, so a host with no resource table refuses it there, naming the
    // field and the ref, in the desktop's words.
    Made device(deviceEcho(), asSettings(msgpackOf(R"({"rom":"res:5"})")));
    Made desktop(desktopEcho(), asSettings(std::string(R"({"rom":"res:5"})")));
    CHECK_FALSE(device.node);
    CHECK(device.refusal == "rom: the host offers no resource table");
    CHECK(device.refusal == desktop.refusal);
}

TEST_CASE("the JSON to msgpack converter writes each number as the kind the JSON spells", "[device-mode]") {
    // An integer literal is an int, in its smallest form; anything with a fraction or an exponent is a float64.
    CHECK(msgpackOf("0") == std::vector<std::uint8_t>{0x00});
    CHECK(msgpackOf("127") == std::vector<std::uint8_t>{0x7f});
    CHECK(msgpackOf("128") == std::vector<std::uint8_t>{0xcc, 0x80});
    CHECK(msgpackOf("-32") == std::vector<std::uint8_t>{0xe0});
    CHECK(msgpackOf("-33") == std::vector<std::uint8_t>{0xd0, 0xdf});
    CHECK(msgpackOf("65536") == std::vector<std::uint8_t>{0xce, 0x00, 0x01, 0x00, 0x00});
    CHECK(msgpackOf("18446744073709551615") ==
          std::vector<std::uint8_t>{0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    CHECK(msgpackOf("2.0") == std::vector<std::uint8_t>{0xcb, 0x40, 0x00, 0, 0, 0, 0, 0, 0});
    CHECK(msgpackOf("1e2").front() == 0xcb);
    CHECK(msgpackOf("null") == std::vector<std::uint8_t>{0xc0});
    CHECK(msgpackOf("[true,false]") == std::vector<std::uint8_t>{0x92, 0xc3, 0xc2});
    CHECK(msgpackOf(R"({"a":"bc"})") == std::vector<std::uint8_t>{0x81, 0xa1, 'a', 0xa2, 'b', 'c'});
    // A long string and a long array take the wider headers.
    const auto longString = msgpackOf("\"" + std::string(40, 'x') + "\"");
    CHECK(longString[0] == 0xd9);
    CHECK(longString[1] == 40);
    std::string array = "[";
    for (int i = 0; i < 20; ++i) array += (i ? ",1" : "1");
    CHECK(msgpackOf(array + "]")[0] == 0xdc);
    // Not JSON, refused.
    CHECK_FALSE(settingsToMsgpack("{"));
}
