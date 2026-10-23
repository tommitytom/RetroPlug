// The registry every package goes through, the graph document's parsing helpers, and resource lifetimes.
#include <catch2/catch_test_macros.hpp>

#include "TestNodes.hpp"
#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/ResourceTable.hpp"

using namespace rp::graph;

namespace {

const char* badDescriptor() { return "{not json"; }
const char* liarDescriptor() { return R"({"id":"rp.test.other","ports":[],"params":[]})"; }

rp_node_type typeWith(const char* id, const char* (*descriptor)()) {
    return rp_node_type{id, descriptor, nullptr, nullptr, nullptr};
}

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
}

TEST_CASE("a bad package registers nothing and says why", "[graph][registry]") {
    NodeRegistry reg;
    std::string error;
    REQUIRE(reg.addPackage(test::testPackage()));
    const std::size_t before = reg.size();

    CHECK_FALSE(reg.addPackage(test::testPackage(), &error));
    CHECK(error.find("already registered") != std::string::npos);

    rp_package wrongAbi = *test::testPackage();
    wrongAbi.abi_version = RP_ABI_VERSION + 1;
    CHECK_FALSE(reg.addPackage(&wrongAbi, &error));
    CHECK(error.find("ABI") != std::string::npos);

    const rp_node_type bad = typeWith("rp.test.bad", &badDescriptor);
    const rp_node_type* badList[] = {&bad};
    const rp_package badPkg{RP_ABI_VERSION, R"({"id":"bad"})", 1, badList};
    CHECK_FALSE(reg.addPackage(&badPkg, &error));
    CHECK(error.find("rp.test.bad") != std::string::npos);

    const rp_node_type liar = typeWith("rp.test.liar", &liarDescriptor);
    const rp_node_type* liarList[] = {&liar};
    const rp_package liarPkg{RP_ABI_VERSION, R"({"id":"liar"})", 1, liarList};
    CHECK_FALSE(reg.addPackage(&liarPkg, &error));
    CHECK(error.find("describes itself") != std::string::npos);

    CHECK(reg.size() == before);
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
    CHECK_FALSE(parseResourceRef("res:"));
    CHECK_FALSE(parseResourceRef("res:0"));
    CHECK_FALSE(parseResourceRef("res:1x"));
    CHECK_FALSE(parseResourceRef("12"));
}

TEST_CASE("a graph document parses, settings and params kept as JSON", "[graph][document]") {
    const auto doc = parseGraphDocument(R"({"nodes":[{"id":"a","type":"rp.core.add","settings":{"inputs":3},
                                             "params":{"g":0.5,"mode":"x","on":true},"resources":{"s":"res:1"}}],
                                             "wires":[{"from":"a.out","to":"b.in","feedback":true}]})");
    REQUIRE(doc);
    REQUIRE(doc->nodes.size() == 1);
    CHECK(doc->nodes[0].settings.has_value());
    CHECK(doc->nodes[0].params->at("mode").to_string().value() == "x");
    CHECK(doc->wires[0].feedback.value());
    CHECK_FALSE(parseGraphDocument(R"({"nodes":[{"type":"x"}],"wires":[]})"));  // a node needs an id
}

TEST_CASE("resources live until their last reference is released", "[graph][resources]") {
    ResourceTable t;
    const auto a = t.createAudioBuffer(2, 64, 48000.0);
    const auto b = t.createBytes("string", {'h', 'i'});
    CHECK(std::string(t.get(a)->type) == "audio-buffer");
    const auto* audio = static_cast<const rp_audio_buffer*>(t.get(a)->payload);
    CHECK(audio->channels == 2);
    CHECK(audio->frames == 64);
    CHECK(audio->data[1][63] == 0.0f);
    CHECK(static_cast<const rp_bytes*>(t.get(b)->payload)->size == 2);
    t.retain(a);
    t.release(a);
    CHECK(t.get(a) != nullptr);
    t.release(a);
    CHECK(t.get(a) == nullptr);
    CHECK(t.liveCount() == 1);
}
