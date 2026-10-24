// The C++ authoring layer (graph/node.hpp): a plain struct becomes an rp_node_type whose descriptor,
// settings handling and optional interfaces behave as docs/modular-graph-design.md section 4 says.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <rfl/DefaultVal.hpp>
#include <rfl/Description.hpp>
#include <rfl/Validator.hpp>
#include <rfl/comparisons.hpp>

#include "TestNodes.hpp"

using namespace rp::graph;

namespace {

enum class SyncMode { Off, MidiSync, HighPassTPDF };

// The conformance node: its TS twin in packages/retroplug/test/graph/conformance.test.ts must produce the
// same descriptor bytes, both compared against golden/rp.test.gain.descriptor.json.
struct Gain {
    static constexpr const char* id = "rp.test.gain";
    static constexpr const char* name = "Gain";
    static constexpr const char* category = "dsp/utility";
    struct { rp::audio_in<rp::any> in; rp::events_in midi; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Gain", rp::range{-60, 12, 0}, "dB"> gain;
        rp::enum_param<SyncMode, "Mode", SyncMode::MidiSync> mode;
        rp::bool_param<"Mute"> mute;
        rp::int_param<"Steps", rp::range{1, 16, 4}> steps;
    } params;
    void process(const rp::Block&) noexcept {}
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

rp_settings json(const char* text) { return rp_settings{text, std::strlen(text), RP_SETTINGS_JSON}; }

struct Made {
    const rp_node_type* type;
    void*               memory = nullptr;
    rp_node*            node = nullptr;
    std::size_t         align = 0;
    ~Made() {
        if (node) type->destroy(node);
        if (memory) ::operator delete(memory, std::align_val_t(align));
    }
};

void make(Made& m, const char* settings) {
    rp_settings s = json(settings);
    rp_memory_req req{};
    rp_error err{};
    REQUIRE(m.type->query_memory(&s, &req, &err));
    m.align = req.align;
    m.memory = ::operator new(req.size, std::align_val_t(req.align));
    m.node = m.type->create(nullptr, m.memory, &s, &err);
    INFO(err.message);
    REQUIRE(m.node);
}

}  // namespace

TEST_CASE("a C++ node's descriptor matches the shared golden byte for byte", "[graph][node]") {
    CHECK(std::string(rp::node_type<Gain>()->descriptor()) == readFile(RP_GRAPH_GOLDEN_DIR "/rp.test.gain.descriptor.json"));
}

namespace {

enum class SyncSource { Off, MidiSync, N8Fifo };

// The second conformance node: every port kind and direction, every channel spec, every param type, an
// int with a unit, a bool defaulting to true, extreme number spellings, appended ports and an exclusive
// group, metadata and capability flags. Its TS twin is in conformance.test.ts.
struct KitchenSink {
    static constexpr const char* id = "rp.test.kitchen-sink";
    static constexpr const char* name = "Kitchen Sink";
    static constexpr const char* category = "test/conformance";
    static constexpr const char* version = "2.1.0";
    static constexpr bool heap = true;
    struct {
        rp::audio_in<rp::any>                in;
        rp::audio_in<rp::channels<2, "any">> side;
        rp::events_in                        midi;
        rp::bytes_in                         serial;
        rp::control_in                       mod;
        rp::link                             peer;
        rp::resource_in<"audio-buffer">      sample;
        rp::resource_write<"audio-buffer">   target;
    } inputs;
    struct {
        rp::audio_out<rp::match<"in">>          out;
        rp::audio_out<rp::channels<6, "5.1">>   surround;
        rp::events_out                          notes;
        rp::bytes_out                           serialOut;
        rp::control_out                         level;
        rp::resource_out<"audio-buffer">        buffer;
    } outputs;
    struct {
        rp::float_param<"Tiny", rp::range{0, 1e-7, 1e-7}>      tiny;
        rp::float_param<"Huge", rp::range{-1e21, 1e21, 0}>     huge;
        rp::int_param<"Steps", rp::range{1, 16, 4}, "steps">   steps;
        rp::bool_param<"Armed", true>                          armed;
        rp::enum_param<SyncSource, "Sync", SyncSource::N8Fifo> sync;
    } params;
    static void adjust_layout(Layout& l, const rp::detail::NoSettings&) {
        for (const char* p : {"pins.a", "pins.b", "channels.x"})
            l.ports.push_back({p, PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        l.exclusive.push_back({"pins.*", "channels.*"});
    }
    void process(const rp::Block&) noexcept {}
};

}  // namespace

TEST_CASE("every descriptor feature matches the shared golden byte for byte", "[graph][node]") {
    const std::string mine = rp::node_type<KitchenSink>()->descriptor();
    CHECK(mine == readFile(RP_GRAPH_GOLDEN_DIR "/rp.test.kitchen-sink.descriptor.json"));
    REQUIRE(parseDescriptor(mine));  // and the strict parser accepts it unchanged
    CHECK(descriptorJson(*parseDescriptor(mine)) == mine);
}

TEST_CASE("enum labels are the enumerator names in kebab-case", "[graph][node]") {
    const std::string d = rp::node_type<Gain>()->descriptor();
    CHECK(d.find(R"("values":["off","midi-sync","high-pass-tpdf"])") != std::string::npos);
    CHECK(d.find(R"("default":"midi-sync")") != std::string::npos);
}

TEST_CASE("settings changes are swapped live unless the layout changes", "[graph][node]") {
    Made m{rp::node_type<test::Source>()};
    make(m, R"({"value":0.5,"channels":2})");
    rp_error err{};
    void* prepared = nullptr;

    rp_settings same = json(R"({"value":0.25,"channels":2})");
    REQUIRE(m.node->vt->prepare_settings(m.node, &same, &prepared, &err) == RP_PREPARE_SWAP);
    void* old = nullptr;
    m.node->vt->commit_settings(m.node, prepared, &old);
    CHECK(rp::peer_as<test::Source>(m.node)->value == 0.25f);
    m.node->vt->release_settings(m.node, old);

    rp_settings wider = json(R"({"value":0.25,"channels":4})");
    CHECK(m.node->vt->prepare_settings(m.node, &wider, &prepared, &err) == RP_PREPARE_REBUILD);

    rp_settings bad = json(R"({"channels":"two"})");
    CHECK(m.node->vt->prepare_settings(m.node, &bad, &prepared, &err) == RP_PREPARE_INVALID);
    CHECK(std::string(err.message).find("channels") != std::string::npos);
}

namespace {

struct PickySettings {
    std::uint32_t voices = 4;
};

struct Picky {
    static constexpr const char* id = "rp.test.picky";
    using Settings = PickySettings;
    static std::optional<std::string> validate(const Settings& s) {
        if (s.voices > 8) return "voices must be 8 or fewer, got " + std::to_string(s.voices);
        return std::nullopt;
    }
    void process(const rp::Block&) noexcept {}
};

struct LinedSettings {
    std::uint32_t length = 16;
};

struct Lined {
    static constexpr const char* id = "rp.test.lined";
    using Settings = LinedSettings;
    static std::size_t extra_memory(const Settings& s) { return s.length * sizeof(float); }
    std::span<std::byte> mem;
    void attach(std::span<std::byte> m) noexcept { mem = m; }
    void process(const rp::Block&) noexcept {}
};

}  // namespace

TEST_CASE("validate rejects settings with a reason", "[graph][node]") {
    const rp_node_type* t = rp::node_type<Picky>();
    rp_settings s = json(R"({"voices":12})");
    rp_memory_req req{};
    rp_error err{};
    CHECK_FALSE(t->query_memory(&s, &req, &err));
    CHECK(std::string(err.message) == "voices must be 8 or fewer, got 12");
    rp_settings ok = json(R"({"voices":8})");
    CHECK(t->query_memory(&ok, &req, &err));
}

TEST_CASE("optional interfaces appear only when the node implements them", "[graph][node]") {
    Made linked{rp::node_type<test::Linked>()};
    make(linked, "{}");
    CHECK(linked.node->vt->get_interface(linked.node, RP_STEP_INTERFACE) != nullptr);
    CHECK(linked.node->vt->get_interface(linked.node, RP_LINK_INTERFACE) != nullptr);
    Made source{rp::node_type<test::Source>()};
    make(source, "{}");
    CHECK(source.node->vt->get_interface(source.node, RP_STEP_INTERFACE) == nullptr);
    CHECK(rp::peer_as<test::Linked>(source.node) == nullptr);
}

TEST_CASE("extra memory is sized by settings and handed to the node", "[graph][node]") {
    const rp_node_type* t = rp::node_type<Lined>();
    rp_settings small = json(R"({"length":10})");
    rp_settings large = json(R"({"length":1000})");
    rp_memory_req a{}, b{};
    rp_error err{};
    REQUIRE(t->query_memory(&small, &a, &err));
    REQUIRE(t->query_memory(&large, &b, &err));
    CHECK(b.size - a.size == 990 * sizeof(float));
    Made m{t};
    make(m, R"({"length":10})");
    CHECK(rp::peer_as<Lined>(m.node)->mem.size() == 10 * sizeof(float));
}

namespace {

// Reflected ports and params bind by position; the ones adjust_layout appends are reached through Block.
struct MixedSettings {
    std::uint32_t aux = 2;
};

struct Mixed {
    static constexpr const char* id = "rp.test.mixed";
    using Settings = MixedSettings;
    struct { rp::audio_in<rp::mono> in; rp::events_in midi; } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;
    struct { rp::float_param<"Level", rp::range{0, 1, 1}> level; } params;

    static void adjust_layout(Layout& l, const Settings& s) {
        // An output appended ahead of inputs: the layer still sorts inputs first, each side in order.
        l.ports.push_back({"tap", PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        for (std::uint32_t k = 0; k < s.aux; ++k)
            l.ports.push_back({"aux" + std::to_string(k + 1), PortDir::In, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        l.params.push_back({"aux-level", "Aux Level", ParamType::Float, 0, 1, 1, {}, {}});
    }

    std::uint32_t aux = 0;
    void on_settings(const Settings& s) noexcept { aux = s.aux; }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t firstAux = 2;  // after in, midi
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            float v = inputs.in[0][i] * static_cast<float>(params.level.start());
            for (std::uint32_t k = 0; k < aux; ++k)
                v += b.input(firstAux + k).u.audio.channels[0][i] * static_cast<float>(b.param(1).start);
            outputs.out[0][i] = v;
            b.output(1).u.audio.channels[0][i] = static_cast<float>(inputs.midi.messages().size());
        }
    }
};

// adjust_layout may patch and append, never move or drop a reflected port.
struct ShufflerSettings {
    bool drop = false;
};

struct Shuffler {
    static constexpr const char* id = "rp.test.shuffler";
    using Settings = ShufflerSettings;
    struct { rp::events_in a; rp::events_in b; } inputs;
    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.drop) l.ports.erase(l.ports.begin());
    }
    void process(const rp::Block&) noexcept {}
};

// Relabels its reflected enum, which would put saved labels out of step with enum_param::value().
struct RelabelSettings {
    bool relabel = false;
};

struct Relabeller {
    static constexpr const char* id = "rp.test.relabeller";
    using Settings = RelabelSettings;
    struct { rp::enum_param<SyncMode, "Mode", SyncMode::Off> mode; } params;
    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.relabel) std::swap(l.params[0].values[0], l.params[0].values[1]);
    }
    void process(const rp::Block&) noexcept {}
};

// A sequencer's position may be called `step`: only a member FUNCTION of a hook's name is a hook.
struct Sequencer {
    static constexpr const char* id = "rp.test.sequencer";
    std::uint32_t step = 0;
    std::uint32_t prepare = 0;
    void process(const rp::Block&) noexcept { ++step; }
};

// Throws building its layout for the default settings, so it has no descriptor to give.
struct Unbuildable {
    static constexpr const char* id = "rp.test.unbuildable";
    static void adjust_layout(Layout&, const rp::detail::NoSettings&) { throw std::runtime_error("no default layout"); }
    void process(const rp::Block&) noexcept {}
};

struct ActivatorSettings {
    std::uint32_t mode = 0;  // 0 succeeds, 1 returns false, 2 throws
};

struct Activator {
    static constexpr const char* id = "rp.test.activator";
    using Settings = ActivatorSettings;
    std::uint32_t mode = 0;
    void on_settings(const Settings& s) noexcept { mode = s.mode; }
    bool activate(double, std::uint32_t) {
        if (mode == 2) throw std::runtime_error("no rom loaded");
        return mode == 0;
    }
    void deactivate() { throw std::runtime_error("ignored"); }
    void process(const rp::Block&) noexcept {}
};

// Reads its settings in its destructor, which is only safe if the node goes before its settings.
float g_seenAtDestroy = 0.0f;

struct Holder {
    static constexpr const char* id = "rp.test.holder";
    using Settings = test::SourceSettings;
    const Settings* settings = nullptr;
    void on_settings(const Settings& s) noexcept { settings = &s; }
    ~Holder() { g_seenAtDestroy = settings ? settings->value : -1.0f; }
    void process(const rp::Block&) noexcept {}
};

template <class P>
P paramReading(rp_control_queue& q, double v) {
    q = rp_control_queue{v, nullptr, 0, 0, 0};
    P p;
    p.queue = &q;
    return p;
}

}  // namespace

TEST_CASE("appended ports and params are reached by index, after the reflected ones", "[graph][node]") {
    Made m{rp::node_type<Mixed>()};
    make(m, R"({"aux":2})");
    const rp_settings settings{R"({"aux":2})", 9, RP_SETTINGS_JSON};
    rp_error err{};
    std::string layout(rp::node_type<Mixed>()->layout_for(&settings, nullptr, 0, &err), '\0');
    REQUIRE(!layout.empty());
    std::string text(layout.size() + 1, '\0');
    REQUIRE(rp::node_type<Mixed>()->layout_for(&settings, text.data(), text.size(), &err) == layout.size());
    layout = text.c_str();
    // inputs first (reflected, then appended), then outputs (reflected, then appended)
    const auto at = [&](const char* id) { return layout.find(std::string("\"id\":\"") + id + "\""); };
    CHECK(at("in") < at("midi"));
    CHECK(at("midi") < at("aux1"));
    CHECK(at("aux2") < at("out"));
    CHECK(at("out") < at("tap"));

    constexpr std::uint32_t kFrames = 4;
    float in[kFrames] = {1, 1, 1, 1}, aux1[kFrames] = {2, 2, 2, 2}, aux2[kFrames] = {3, 3, 3, 3};
    float out[kFrames] = {}, tap[kFrames] = {};
    float* inCh[] = {in};
    float* aux1Ch[] = {aux1};
    float* aux2Ch[] = {aux2};
    float* outCh[] = {out};
    float* tapCh[] = {tap};
    rp_message msgs[1] = {};
    rp_queue midi{};
    midi.items = msgs;
    midi.count = 1;
    rp_port_view inputs[4] = {};
    inputs[0].kind = RP_PORT_AUDIO;
    inputs[0].u.audio = {inCh, 1};
    inputs[1].kind = RP_PORT_EVENTS;
    inputs[1].u.queue = &midi;
    inputs[2].kind = RP_PORT_AUDIO;
    inputs[2].u.audio = {aux1Ch, 1};
    inputs[3].kind = RP_PORT_AUDIO;
    inputs[3].u.audio = {aux2Ch, 1};
    rp_port_view outputs[2] = {};
    outputs[0].kind = RP_PORT_AUDIO;
    outputs[0].u.audio = {outCh, 1};
    outputs[1].kind = RP_PORT_AUDIO;
    outputs[1].u.audio = {tapCh, 1};
    rp_control_queue level{0.5, nullptr, 0, 0, 0}, auxLevel{0.25, nullptr, 0, 0, 0};
    rp_control_queue* params[] = {&level, &auxLevel};
    rp_transport transport{};
    const rp_process block{kFrames, &transport, inputs, 4, outputs, 2, params, 2};
    REQUIRE(m.node->vt->process(m.node, &block) == RP_OK);
    CHECK(out[0] == 0.5f + (2.0f + 3.0f) * 0.25f);
    CHECK(tap[3] == 1.0f);
}

TEST_CASE("adjust_layout cannot move or drop a reflected port", "[graph][node]") {
    const rp_node_type* t = rp::node_type<Shuffler>();
    CHECK(std::string(t->descriptor()).find(R"("id":"rp.test.shuffler")") != std::string::npos);
    rp_settings drop = json(R"({"drop":true})");
    rp_memory_req req{};
    rp_error err{};
    REQUIRE(t->query_memory(&drop, &req, &err));
    void* memory = ::operator new(req.size, std::align_val_t(req.align));
    CHECK(t->create(nullptr, memory, &drop, &err) == nullptr);
    CHECK(std::string(err.message).find("adjust_layout") != std::string::npos);
    ::operator delete(memory, std::align_val_t(req.align));

    Made m{t};
    make(m, "{}");
    void* prepared = nullptr;
    err = rp_error{};
    CHECK(m.node->vt->prepare_settings(m.node, &drop, &prepared, &err) == RP_PREPARE_INVALID);
    CHECK(std::string(err.message).find("\"a\"") != std::string::npos);

    CHECK(std::string(rp::node_type<Unbuildable>()->descriptor()).empty());  // the registry refuses it

    Made relabeller{rp::node_type<Relabeller>()};
    make(relabeller, "{}");
    rp_settings relabel = json(R"({"relabel":true})");
    err = rp_error{};
    CHECK(relabeller.node->vt->prepare_settings(relabeller.node, &relabel, &prepared, &err) == RP_PREPARE_INVALID);
    CHECK(std::string(err.message).find("relabelled reflected param \"mode\"") != std::string::npos);
}

TEST_CASE("a data member named like a hook is not a hook", "[graph][node]") {
    Made m{rp::node_type<Sequencer>()};
    make(m, "{}");
    CHECK(m.node->vt->get_interface(m.node, RP_STEP_INTERFACE) == nullptr);
}

TEST_CASE("activate failures are reported through err, never thrown", "[graph][node]") {
    for (const auto& [settings, ok, reason] : {std::tuple{R"({"mode":0})", true, ""},
                                               std::tuple{R"({"mode":1})", false, "activate failed"},
                                               std::tuple{R"({"mode":2})", false, "no rom loaded"}}) {
        Made m{rp::node_type<Activator>()};
        make(m, settings);
        rp_error err{};
        CHECK(m.node->vt->activate(m.node, 48000.0, 256, &err) == ok);
        CHECK(std::string(err.message) == reason);
        m.node->vt->deactivate(m.node);  // throws inside, and must not escape
    }
}

TEST_CASE("a settings-less node still refuses what is not a JSON object", "[graph][node]") {
    const rp_node_type* t = rp::node_type<test::Sink>();
    rp_memory_req req{};
    rp_error err{};
    rp_settings empty{nullptr, 0, RP_SETTINGS_JSON};
    CHECK(t->query_memory(&empty, &req, &err));
    rp_settings object = json("{}");
    CHECK(t->query_memory(&object, &req, &err));
    rp_settings array = json("[1]");
    CHECK_FALSE(t->query_memory(&array, &req, &err));
    rp_settings garbage = json("not json");
    CHECK_FALSE(t->query_memory(&garbage, &req, &err));
    rp_settings other{"{}", 2, 7};
    CHECK_FALSE(t->query_memory(&other, &req, &err));
    CHECK(std::string(err.message).find("encoding") != std::string::npos);
}

TEST_CASE("a node is destroyed before its settings", "[graph][node]") {
    g_seenAtDestroy = 0.0f;
    {
        Made m{rp::node_type<Holder>()};
        make(m, R"({"value":0.75})");
    }
    CHECK(g_seenAtDestroy == 0.75f);
}

TEST_CASE("param values never read undefined, whatever the wire carries", "[graph][node]") {
    // int rounds and saturates to int (not to its declared range); enum clamps to its labels; NaN is the default
    using Steps = rp::int_param<"Steps", rp::range{1, 16, 4}>;
    using Mode = rp::enum_param<SyncMode, "Mode", SyncMode::MidiSync>;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    rp_control_queue q{};
    CHECK(paramReading<Steps>(q, nan).value() == 4);
    CHECK(paramReading<Steps>(q, inf).value() == std::numeric_limits<int>::max());
    CHECK(paramReading<Steps>(q, -1e300).value() == std::numeric_limits<int>::min());
    CHECK(paramReading<Steps>(q, 2.5).value() == 3);
    CHECK(paramReading<Steps>(q, -2.5).value() == -3);
    CHECK(paramReading<Mode>(q, nan).value() == SyncMode::MidiSync);
    CHECK(paramReading<Mode>(q, inf).value() == SyncMode::HighPassTPDF);
    CHECK(paramReading<Mode>(q, -inf).value() == SyncMode::Off);
    CHECK(paramReading<Mode>(q, 1e300).value() == SyncMode::HighPassTPDF);
    CHECK(paramReading<Mode>(q, 1.4).value() == SyncMode::MidiSync);
    CHECK(paramReading<Mode>(q, 1.6).value() == SyncMode::HighPassTPDF);
    CHECK(paramReading<Mode>(q, -0.7).value() == SyncMode::Off);
    CHECK_FALSE(paramReading<rp::bool_param<"Mute">>(q, nan).value());
    CHECK(paramReading<rp::bool_param<"Mute">>(q, 0.5).value());
}

// What the reflect-cpp 0.26 upgrade was for: schema defaults, ranges and descriptions, from the settings
// struct alone, and settings refused (with reflect-cpp's reason) when they break a declared range.
namespace nodelayer {
struct RichSettings {
    rfl::DefaultVal<std::uint32_t>                            voices = 2;
    rfl::Validator<int, rfl::Minimum<20>, rfl::Maximum<100>> rate = 30;
    rfl::Description<"How loud", double>                     level = 0.5;
};
}  // namespace nodelayer

namespace {
struct Rich {
    static constexpr const char* id = "rp.test.rich";
    using Settings = nodelayer::RichSettings;
    std::uint32_t voices = 0;
    int           rate = 0;
    void on_settings(const Settings& s) noexcept {
        voices = s.voices.value();
        rate = s.rate.value();
    }
    void process(const rp::Block&) noexcept {}
};
}  // namespace

TEST_CASE("settings schemas carry defaults, ranges and descriptions, and ranges are enforced", "[graph][node]") {
    const std::string d = rp::node_type<Rich>()->descriptor();
    CHECK(d.find(R"("default":2)") != std::string::npos);
    CHECK(d.find(R"("minimum":20)") != std::string::npos);
    CHECK(d.find(R"("maximum":100)") != std::string::npos);
    CHECK(d.find(R"("description":"How loud")") != std::string::npos);
    REQUIRE(parseDescriptor(d));  // a schema the registry accepts

    Made m{rp::node_type<Rich>()};
    make(m, "{}");
    CHECK(rp::peer_as<Rich>(m.node)->voices == 2);  // defaults apply
    CHECK(rp::peer_as<Rich>(m.node)->rate == 30);
    rp_settings low = json(R"({"rate":5})");
    rp_memory_req req{};
    rp_error err{};
    CHECK_FALSE(m.type->query_memory(&low, &req, &err));
    CHECK(std::string(err.message).find("20") != std::string::npos);
}
