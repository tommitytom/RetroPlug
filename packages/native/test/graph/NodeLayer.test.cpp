// The C++ authoring layer (graph/node.hpp): a plain struct becomes an rp_node_type whose descriptor,
// settings handling and optional interfaces behave as docs/modular-graph-design.md section 4 says.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <new>
#include <sstream>

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
