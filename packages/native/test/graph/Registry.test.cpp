// The registry every package goes through, the graph document's parsing helpers, and resource lifetimes.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "TestNodes.hpp"
#include "graph/CanonicalJson.hpp"
#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/ResourceTable.hpp"

using namespace rp::graph;

namespace {

const char* goodDescriptor(const rp_node_type*) { return R"({"id":"rp.test.good"})"; }
const char* badDescriptor(const rp_node_type*) { return "{not json"; }
const char* liarDescriptor(const rp_node_type*) { return R"({"id":"rp.test.other","ports":[],"params":[]})"; }
const char* nullDescriptor(const rp_node_type*) { return nullptr; }
const char* sourceDescriptor(const rp_node_type*) { return R"({"id":"rp.test.source"})"; }

bool noMemory(const rp_node_type*, const rp_settings*, rp_memory_req*, rp_error*) { return false; }
std::size_t noLayout(const rp_node_type*, const rp_settings*, char*, std::size_t, rp_error*) { return 0; }
rp_node* noCreate(const rp_node_type*, const rp_host*, void*, const rp_settings*, rp_error*) { return nullptr; }
void noDestroy(rp_node*) {}

// A node type whose functions are stubs: these tests only register types, never build them.
rp_node_type typeWith(const char* id, const char* (*descriptor)(const rp_node_type*)) {
    return rp_node_type{id, descriptor, &noLayout, &noMemory, &noCreate, &noDestroy, 0u};
}

std::string refusal(NodeRegistry& reg, std::initializer_list<const rp_node_type*> types,
                    const char* manifest = R"({"id":"p"})") {
    const std::vector<const rp_node_type*> list(types);
    const rp_package pkg{RP_ABI_VERSION, manifest, static_cast<std::uint32_t>(list.size()), list.data()};
    std::string error;
    return reg.addPackage(&pkg, &error) ? std::string("registered") : error;
}

// rp.test.n0 ... rp.test.n63, each with a descriptor function of its own (a C function pointer cannot
// capture which one it is).
template <std::size_t N>
const char* numberedDescriptor(const rp_node_type*) {
    static const std::string json = R"({"id":"rp.test.n)" + std::to_string(N) + R"("})";
    return json.c_str();
}

template <std::size_t... N>
constexpr std::array<const char* (*)(const rp_node_type*), sizeof...(N)> numberedDescriptors(std::index_sequence<N...>) {
    return {&numberedDescriptor<N>...};
}

constexpr auto kNumbered = numberedDescriptors(std::make_index_sequence<64>{});

// Throws building its default layout, so node.hpp gives it an empty descriptor.
struct Unbuildable {
    static constexpr const char* id = "rp.test.unbuildable";
    static void adjust_layout(Layout&, const rp::detail::NoSettings&) { throw std::runtime_error("no default layout"); }
    void process(const rp::Block&) noexcept {}
};

}  // namespace

TEST_CASE("a package registers every node type it lists", "[graph][registry]") {
    NodeRegistry reg;
    std::string error;
    REQUIRE(reg.addPackage(test::testPackage(), &error));
    const auto* e = reg.find("rp.test.source");
    REQUIRE(e);
    CHECK(e->package == "rp.test");
    CHECK(e->descriptor.layout.params[0].id == "gain");
    CHECK(reg.find("rp.nope") == nullptr);

    // Entries never move: one held across enough later registrations to regrow any contiguous store.
    // (The registry keeps pointers to the types, so they stay alive while it is used, as a package's do.)
    std::vector<std::string> ids;
    std::array<rp_node_type, kNumbered.size()> types{};
    for (std::size_t i = 0; i < kNumbered.size(); ++i) ids.push_back("rp.test.n" + std::to_string(i));
    for (std::size_t i = 0; i < kNumbered.size(); ++i) {
        types[i] = typeWith(ids[i].c_str(), kNumbered[i]);
        const std::string manifest = R"({"id":"p)" + std::to_string(i) + R"("})";
        REQUIRE(refusal(reg, {&types[i]}, manifest.c_str()) == "registered");
    }
    CHECK(std::string(reg.find("rp.test.n3")->type->id) == "rp.test.n3");
    CHECK(reg.find("rp.test.source") == e);
    CHECK(e->descriptor.id == "rp.test.source");
    CHECK(reg.size() == 8 + kNumbered.size());
}

TEST_CASE("a bad package registers nothing and says why", "[graph][registry]") {
    NodeRegistry reg;
    std::string error;
    REQUIRE(reg.addPackage(test::testPackage()));
    const std::size_t before = reg.size();

    CHECK_FALSE(reg.addPackage(test::testPackage(), &error));
    CHECK(error == "package \"rp.test\" is already registered");

    rp_package wrongAbi = *test::testPackage();
    wrongAbi.abi_version = RP_ABI_VERSION + 1;
    CHECK_FALSE(reg.addPackage(&wrongAbi, &error));
    CHECK(error.find("ABI") != std::string::npos);

    const rp_node_type bad = typeWith("rp.test.bad", &badDescriptor);
    CHECK(refusal(reg, {&bad}).find("node type \"rp.test.bad\": ") == 0);
    const rp_node_type liar = typeWith("rp.test.liar", &liarDescriptor);
    CHECK(refusal(reg, {&liar}) == "node type \"rp.test.liar\" describes itself as \"rp.test.other\"");
    const rp_node_type source = typeWith("rp.test.source", &sourceDescriptor);  // another package's type id
    CHECK(refusal(reg, {&source}) == "node type \"rp.test.source\" is already registered");

    // All or nothing: a good type ahead of a bad one is not kept.
    const rp_node_type good = typeWith("rp.test.good", &goodDescriptor);
    CHECK(refusal(reg, {&good, &bad}).find("rp.test.bad") != std::string::npos);
    CHECK(reg.find("rp.test.good") == nullptr);
    CHECK(refusal(reg, {&good, &good}) == "node type \"rp.test.good\" appears twice in package \"p\"");
    CHECK(reg.find("rp.test.good") == nullptr);

    CHECK(reg.size() == before);
}

TEST_CASE("addPackage checks everything the host will later call or read", "[graph][registry]") {
    NodeRegistry reg;
    rp_node_type noCreateFn = typeWith("rp.test.x", &goodDescriptor);
    noCreateFn.create = nullptr;
    CHECK(refusal(reg, {&noCreateFn}) == "node type \"rp.test.x\": create is null");
    rp_node_type noDestroyFn = typeWith("rp.test.x", &goodDescriptor);
    noDestroyFn.destroy = nullptr;
    CHECK(refusal(reg, {&noDestroyFn}) == "node type \"rp.test.x\": destroy is null");
    rp_node_type noLayoutFn = typeWith("rp.test.x", &goodDescriptor);
    noLayoutFn.layout_for = nullptr;
    CHECK(refusal(reg, {&noLayoutFn}) == "node type \"rp.test.x\": layout_for is null");
    rp_node_type noQuery = typeWith("rp.test.x", &goodDescriptor);
    noQuery.query_memory = nullptr;
    CHECK(refusal(reg, {&noQuery}) == "node type \"rp.test.x\": query_memory is null");
    const rp_node_type nullJson = typeWith("rp.test.x", &nullDescriptor);
    CHECK(refusal(reg, {&nullJson}) == "node type \"rp.test.x\": descriptor() returned null");
    const rp_node_type noId = typeWith(nullptr, &goodDescriptor);
    CHECK(refusal(reg, {&noId}) == "package \"p\" node type #0 has no id");
    CHECK(refusal(reg, {nullptr}) == "package \"p\" node type #0 is null");
    CHECK(refusal(reg, {rp::node_type<Unbuildable>()}).find("produced no descriptor") != std::string::npos);

    const rp_package noArray{RP_ABI_VERSION, R"({"id":"p"})", 2, nullptr};
    std::string error;
    CHECK_FALSE(reg.addPackage(&noArray, &error));
    CHECK(error.find("no array") != std::string::npos);

    const rp_node_type good = typeWith("rp.test.good", &goodDescriptor);
    CHECK(refusal(reg, {&good}, nullptr) == "package has no manifest");
    CHECK(refusal(reg, {&good}, "{not json").find("package manifest: ") == 0);
    CHECK(refusal(reg, {&good}, R"({"id":5})").find("package manifest: ") == 0);
    CHECK(refusal(reg, {&good}, R"({"id":"a b"})").find("no whitespace") != std::string::npos);
    CHECK(refusal(reg, {&good}, R"({})").find("package manifest: ") == 0);
    CHECK(reg.size() == 0);
    CHECK(refusal(reg, {&good}, R"({"id":"a","id":"b"})") == "registered");  // the last "id" wins, as in JSON.parse
    CHECK(reg.find("rp.test.good")->package == "b");

    // Every flags bit is a relaxation, so a type setting bits this host does not know still registers.
    NodeRegistry newer;
    rp_node_type future = typeWith("rp.test.good", &goodDescriptor);
    future.flags = ~0u;
    CHECK(refusal(newer, {&future}) == "registered");
}

namespace {

// The versions of a package "p" the replacement cases swap: p.a in each (its name telling which), p.b in the first, p.c
// in the second.
const char* aOne(const rp_node_type*) { return R"({"id":"p.a","name":"A one"})"; }
const char* aTwo(const rp_node_type*) { return R"({"id":"p.a","name":"A two"})"; }
const char* aThree(const rp_node_type*) { return R"({"id":"p.a","name":"A three"})"; }
const char* bOne(const rp_node_type*) { return R"({"id":"p.b"})"; }
const char* cTwo(const rp_node_type*) { return R"({"id":"p.c"})"; }
const char* qA(const rp_node_type*) { return R"({"id":"q.a"})"; }

// One version of a package: its types, held where the package's array points, and the package.
struct Version {
    std::vector<rp_node_type>        types;
    std::vector<const rp_node_type*> list;
    rp_package                       package{};
    Version(const char* manifest, std::vector<rp_node_type> t) : types(std::move(t)) {
        for (const auto& type : types) list.push_back(&type);
        package = rp_package{RP_ABI_VERSION, manifest, static_cast<std::uint32_t>(list.size()), list.data()};
    }
    Version(const Version&) = delete;
    Version& operator=(const Version&) = delete;
};

}  // namespace

TEST_CASE("a package is replaced by a new version of itself: its old entries stay valid until it is put back as it "
          "was, or they are dropped",
          "[graph][registry]") {
    NodeRegistry reg;
    REQUIRE(reg.addPackage(test::testPackage()));
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo), typeWith("p.c", &cTwo)});
    const Version three(R"({"id":"p"})", {typeWith("p.a", &aThree)});
    REQUIRE(reg.addPackage(&one.package));
    CHECK(reg.hasPackage("p"));
    const auto* a1 = reg.find("p.a");
    const auto* b1 = reg.find("p.b");
    REQUIRE(a1);
    REQUIRE(b1);
    const std::size_t total = reg.size();

    // The swap: p.a is a new entry (the planner rebuilds an instance whose entry changed), p.b is gone, p.c is found,
    // and what a planned instance of the first version holds is still there to read.
    std::string error;
    CHECK(reg.replacePackage("p", &two.package, &error) == &one.package);
    INFO(error);
    const auto* a2 = reg.find("p.a");
    REQUIRE(a2);
    CHECK(a2 != a1);
    CHECK(a2->type == &two.types[0]);
    CHECK(a2->descriptor.name == "A two");
    CHECK(reg.find("p.b") == nullptr);
    REQUIRE(reg.find("p.c"));
    CHECK(reg.find("p.c")->package == "p");
    CHECK(a1->descriptor.name == "A one");
    CHECK(a1->type == &one.types[0]);
    CHECK(b1->descriptor.id == "p.b");
    CHECK(reg.size() == total);
    CHECK(reg.retiredSize() == 2);

    // Undone: the first version's entries found again, the very ones, and the second's gone.
    CHECK(reg.restorePackage(&one.package));
    CHECK(reg.find("p.a") == a1);
    CHECK(reg.find("p.b") == b1);
    CHECK(reg.find("p.c") == nullptr);
    CHECK(reg.retiredSize() == 0);
    CHECK_FALSE(reg.restorePackage(&one.package));  // retired no more

    // Two swaps in a row retire two versions; only the last can be undone, and each is dropped on its own.
    CHECK(reg.replacePackage("p", &two.package) == &one.package);
    const auto* a2again = reg.find("p.a");
    CHECK(reg.replacePackage("p", &three.package) == &two.package);
    CHECK(reg.find("p.a")->descriptor.name == "A three");
    CHECK(reg.find("p.c") == nullptr);
    CHECK(reg.retiredSize() == 4);
    CHECK_FALSE(reg.restorePackage(&one.package));  // the second version replaced it, not the third
    CHECK(reg.restorePackage(&two.package));
    CHECK(reg.find("p.a") == a2again);
    CHECK(reg.find("p.c")->descriptor.id == "p.c");
    CHECK(reg.retiredSize() == 2);
    CHECK(reg.dropRetired(&one.package));
    CHECK_FALSE(reg.dropRetired(&one.package));
    CHECK(reg.retiredSize() == 0);
    CHECK(reg.find("p.a") == a2again);
    CHECK(reg.replacePackage("p", &three.package) == &two.package);
    CHECK(reg.dropRetired(&two.package));
    CHECK(reg.retiredSize() == 0);
    CHECK(reg.find("p.a")->type == &three.types[0]);
    CHECK(reg.size() == total - 1);  // p.a alone, where the first version had p.b too
}

TEST_CASE("a replacement refused changes nothing: a bad descriptor, another package's id, a type id another package "
          "holds, a package not registered, a version held already",
          "[graph][registry]") {
    NodeRegistry reg;
    REQUIRE(reg.addPackage(test::testPackage()));
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo)});
    REQUIRE(reg.addPackage(&one.package));
    REQUIRE(reg.replacePackage("p", &two.package) == &one.package);
    const auto* a = reg.find("p.a");
    const auto* source = reg.find("rp.test.source");
    const std::size_t total = reg.size();

    const auto refused = [&](const char* id, const rp_package* next, const std::string& why) {
        INFO(why);
        std::string error;
        CHECK(reg.replacePackage(id, next, &error) == nullptr);
        CHECK(error.find(why) == 0);
        CHECK(reg.find("p.a") == a);
        CHECK(reg.find("p.b") == nullptr);
        CHECK(reg.find("rp.test.source") == source);
        CHECK(reg.size() == total);
        CHECK(reg.retiredSize() == 2);
    };
    const Version bad(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("p.x", &badDescriptor)});
    refused("p", &bad.package, "node type \"p.x\": ");
    const Version other(R"({"id":"rp.test"})", {typeWith("p.a", &aThree)});
    refused("p", &other.package,
            "package \"rp.test\" cannot replace package \"p\": a new version keeps its package's id");
    const Version stray(R"({"id":"q"})", {typeWith("p.a", &aThree)});
    refused("p", &stray.package, "package \"q\" cannot replace package \"p\"");
    const Version taker(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("rp.test.source", &sourceDescriptor)});
    refused("p", &taker.package, "node type \"rp.test.source\" is already registered");
    refused("q", &stray.package, "package \"q\" is not registered, so it has no version to replace");
    refused("p", &two.package, "that version of package \"p\" is registered already");
    refused("p", &one.package, "that version of package \"p\" is registered already");  // retired, not yet dropped
    refused("p", nullptr, "null package");

    // A package's own type ids are the new version's to take again; the replacement that keeps every one goes through.
    const Version three(R"({"id":"p"})", {typeWith("p.a", &aThree)});
    CHECK(reg.replacePackage("p", &three.package) == &two.package);
    CHECK(reg.find("p.a")->descriptor.name == "A three");
}

TEST_CASE("a replacement with no package id is refused, changing nothing: never taken for an addition",
          "[graph][registry]") {
    NodeRegistry reg;
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo)});
    const Version fresh(R"({"id":"q"})", {typeWith("q.a", &qA)});  // a package no one registered, every type id free
    REQUIRE(reg.addPackage(&one.package));
    const auto* a = reg.find("p.a");
    for (const rp_package* next : {&fresh.package, &two.package}) {
        std::string error;
        CHECK(reg.replacePackage("", next, &error) == nullptr);
        CHECK(error == "package \"\" is not registered, so it has no version to replace");
        CHECK_FALSE(reg.hasPackage("q"));
        CHECK(reg.find("q.a") == nullptr);
        CHECK(reg.find("p.a") == a);
        CHECK(reg.size() == 1);
        CHECK(reg.retiredSize() == 0);
    }
    CHECK(reg.addPackage(&fresh.package));  // still free to be added
}

TEST_CASE("a package's current version is no retired one: dropRetired refuses it, changing nothing",
          "[graph][registry]") {
    NodeRegistry reg;
    REQUIRE(reg.addPackage(test::testPackage()));
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo)});
    REQUIRE(reg.addPackage(&one.package));
    const auto* a1 = reg.find("p.a");
    const auto* b1 = reg.find("p.b");
    REQUIRE(reg.replacePackage("p", &two.package) == &one.package);
    const auto* a2 = reg.find("p.a");
    const std::size_t total = reg.size();

    CHECK_FALSE(reg.dropRetired(&two.package));
    CHECK_FALSE(reg.dropRetired(test::testPackage()));  // registered, never replaced
    CHECK(reg.find("p.a") == a2);
    CHECK(reg.size() == total);
    CHECK(reg.retiredSize() == 2);  // the first version's two

    // Its retired version is untouched: still put back, the very entries.
    CHECK(reg.restorePackage(&one.package));
    CHECK(reg.find("p.a") == a1);
    CHECK(reg.find("p.b") == b1);
    CHECK_FALSE(reg.dropRetired(&one.package));  // current again
    CHECK(reg.find("p.a") == a1);
    CHECK(reg.retiredSize() == 0);
}

TEST_CASE("a retired version is not put back once another package has taken one of its type ids, changing nothing",
          "[graph][registry]") {
    NodeRegistry reg;
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo), typeWith("p.c", &cTwo)});
    const Version taker(R"({"id":"q"})", {typeWith("p.b", &bOne)});  // the id the second version gave up
    REQUIRE(reg.addPackage(&one.package));
    REQUIRE(reg.replacePackage("p", &two.package) == &one.package);
    REQUIRE(reg.addPackage(&taker.package));
    const auto* a2 = reg.find("p.a");
    const auto* b = reg.find("p.b");
    const auto* c2 = reg.find("p.c");
    REQUIRE(b);
    CHECK(b->package == "q");
    const std::size_t total = reg.size();

    CHECK_FALSE(reg.restorePackage(&one.package));
    CHECK(reg.find("p.a") == a2);
    CHECK(reg.find("p.b") == b);
    CHECK(reg.find("p.c") == c2);
    CHECK(reg.size() == total);
    CHECK(reg.retiredSize() == 2);
    // Still retired, so still dropped, and the second version stays.
    CHECK(reg.dropRetired(&one.package));
    CHECK(reg.retiredSize() == 0);
    CHECK(reg.find("p.a") == a2);
}

TEST_CASE("a refused replacement leaves the last one undoable: the retired version is put back as it was",
          "[graph][registry]") {
    NodeRegistry reg;
    REQUIRE(reg.addPackage(test::testPackage()));
    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo), typeWith("p.c", &cTwo)});
    REQUIRE(reg.addPackage(&one.package));
    const auto* a1 = reg.find("p.a");
    const auto* b1 = reg.find("p.b");
    REQUIRE(reg.replacePackage("p", &two.package) == &one.package);
    const std::size_t total = reg.size();

    // Refused for each kind of reason: a bad descriptor, a type id another package holds, a version held already (the
    // retired one itself among them), another package's id.
    const Version bad(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("p.x", &badDescriptor)});
    const Version taker(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("rp.test.source", &sourceDescriptor)});
    const Version stray(R"({"id":"q"})", {typeWith("p.a", &aThree)});
    for (const rp_package* next : {&bad.package, &taker.package, &two.package, &one.package, &stray.package}) {
        CHECK(reg.replacePackage("p", next) == nullptr);
        CHECK(reg.size() == total);
        CHECK(reg.retiredSize() == 2);
    }

    CHECK(reg.restorePackage(&one.package));
    CHECK(reg.find("p.a") == a1);
    CHECK(reg.find("p.b") == b1);
    CHECK(reg.find("p.c") == nullptr);
    CHECK(reg.find("p.a")->descriptor.name == "A one");
    CHECK(reg.retiredSize() == 0);
    CHECK(reg.size() == total);
}

TEST_CASE("endpoints split at the first dot; resource refs parse", "[graph][document]") {
    const auto e = parseEndpoint("gb1.pins.pulse");
    REQUIRE(e);
    CHECK(e->node == "gb1");
    CHECK(e->port == "pins.pulse");
    CHECK_FALSE(parseEndpoint("gb1"));
    CHECK_FALSE(parseEndpoint(".x"));
    CHECK_FALSE(parseEndpoint("x."));
    CHECK(parseResourceRef("res:12") == 12u);
    CHECK(parseResourceRef("res:4294967295") == 4294967295u);
    CHECK_FALSE(parseResourceRef("res:4294967296"));
    CHECK_FALSE(parseResourceRef("res:"));
    CHECK_FALSE(parseResourceRef("res:0"));
    CHECK_FALSE(parseResourceRef("res:007"));
    CHECK_FALSE(parseResourceRef("res:1x"));
    CHECK_FALSE(parseResourceRef("res:+1"));
    CHECK_FALSE(parseResourceRef("12"));
}

TEST_CASE("a graph document parses, settings and params kept as JSON", "[graph][document]") {
    const auto doc = parseGraphDocument(R"({"nodes":[{"id":"a","type":"rp.core.add","settings":{"inputs":3,"big":18446744073709551615},
                                             "params":{"g":0.5,"mode":"x","on":true},"resources":{"s":"res:1"}}],
                                             "wires":[{"from":"a.out","to":"b.in","feedback":true}]})");
    REQUIRE(doc);
    REQUIRE(doc->nodes.size() == 1);
    CHECK(doc->nodes[0].settings.has_value());
    CHECK(doc->nodes[0].params->at("mode").to_string().value() == "x");
    CHECK(doc->wires[0].feedback.value());
    // read as JSON.parse reads it, not wrapped to a negative int64
    CHECK(doc->nodes[0].settings->to_object()->get("big")->to_double().value() > 0.0);

    const auto noId = parseGraphDocument(R"({"nodes":[{"type":"x"}],"wires":[]})");
    REQUIRE_FALSE(noId);
    CHECK(std::string(noId.error().what()).find("graph document: ") == 0);
    CHECK_FALSE(parseGraphDocument(R"({"nodes":[{"id":"a","type":"x","setings":{}}],"wires":[]})"));  // a typo is refused
    CHECK_FALSE(parseGraphDocument("[]"));
}

TEST_CASE("the document TS spells is the document the planner parses", "[graph][document]") {
    // golden/graph-document.json is written by packages/retroplug/src/graph/graphDocument.ts toGraphJson
    // (its test holds it to that); here the planner's parser reads it.
    std::ifstream in(RP_GRAPH_GOLDEN_DIR "/graph-document.json");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const auto doc = parseGraphDocument(text);
    INFO((doc ? "" : doc.error().what()));
    REQUIRE(doc);
    REQUIRE(doc->nodes.size() == 4);
    CHECK(doc->nodes[0].id == "sys-1");
    CHECK(doc->nodes[0].params->at("mode").to_string().value() == "midi-sync");
    CHECK(doc->nodes[0].params->at("mute").to_bool().value() == false);
    CHECK(doc->nodes[0].settings->to_object()->get("rom")->to_object()->get("path")->to_string().value() == "lsdj.gb");
    CHECK(doc->nodes[1].id == "sys-1/lsdj-sync");
    CHECK(parseResourceRef(doc->nodes[2].resources->at("sample")) == 12u);
    REQUIRE(doc->wires.size() == 2);
    CHECK(doc->wires[0].feedback.value());
    CHECK_FALSE(doc->wires[1].feedback.has_value());
    CHECK(canonicalizeJson(text).value() == text);  // and C++ spells it the same way back
}

TEST_CASE("a resource lives until its owner drops it and its last pin is gone", "[graph][resources]") {
    ResourceTable t;
    const auto a = t.createAudioBuffer(2, 64, 48000.0);
    const auto b = t.createBytes("string", {'h', 'i'});
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    CHECK(std::string(t.get(a)->type) == "audio-buffer");
    const auto* audio = static_cast<const rp_audio_buffer*>(t.get(a)->payload);
    CHECK(audio->channels == 2);
    CHECK(audio->frames == 64);
    CHECK(audio->data[1][63] == 0.0f);
    CHECK(static_cast<const rp_bytes*>(t.get(b)->payload)->size == 2);

    REQUIRE(t.pin(a));
    CHECK(t.drop(a));
    CHECK_FALSE(t.drop(a));  // a second drop changes nothing
    CHECK(t.pinCount(a) == 1);
    CHECK(t.liveCount() == 2);  // alive for the plan that pins it...
    CHECK(t.get(a) == nullptr);  // ...but no new document can bind it
    CHECK_FALSE(t.pin(a));
    CHECK(t.unpin(a));
    CHECK(t.liveCount() == 1);
    CHECK_FALSE(t.unpin(a));

    const auto c = t.createBytes("bytes", {});
    CHECK(c > b);  // ids are never reused
    CHECK(t.drop(c));
    CHECK(t.get(c) == nullptr);
    CHECK_FALSE(t.drop(999));
}

TEST_CASE("resources are refused when their payload would not match their type", "[graph][resources]") {
    ResourceTable t;
    std::string error;
    CHECK(t.createBytes("audio-buffer", {1, 2, 3}, &error) == 0);
    CHECK(error.find("createAudioBuffer") != std::string::npos);
    CHECK(t.createBytes("", {}, &error) == 0);
    CHECK(t.createBytes("my type", {}, &error) == 0);
    CHECK(t.createBytes(std::string("audio-buffer\0", 13), {1, 2, 3}, &error) == 0);  // reads as "audio-buffer" in C
    CHECK(error.find("control characters") != std::string::npos);
    CHECK(t.createBytes("audio\x01" "buffer", {}, &error) == 0);
    CHECK(error.find(R"("audio\u0001buffer")") != std::string::npos);  // quoted escaped, so it prints intact
    CHECK(t.createAudioBuffer(0, 64, 48000.0, &error) == 0);
    CHECK(t.createAudioBuffer(2, 0, 48000.0, &error) == 0);
    CHECK(t.createAudioBuffer(2, 64, 0.0, &error) == 0);
    CHECK(t.createAudioBuffer(2, 64, std::numeric_limits<double>::quiet_NaN(), &error) == 0);
    CHECK(t.createAudioBuffer(65536, 65536, 48000.0, &error) == 0);
    CHECK(error.find("larger than") != std::string::npos);
    CHECK(t.liveCount() == 0);
}
