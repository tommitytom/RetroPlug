// Reshape (docs/modular-graph-phase-b3a-design.md section 2.4): a node may keep its instance through a settings
// change that alters its layout. The new plan's ports are bound to it by id, what its params and control outputs
// hold carries over by id, a feedback wire into or out of it keeps its block in flight, and its new settings commit
// at the swap, in push order with every other command. A node that answers rebuild is rebuilt as before, a
// document refused after a reshape was prepared leaves the instance as it was, and the commits of a node whose
// prepare is stateless never make setGraph wait.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;
using rp::graph::test::Shape;
using rp::graph::test::Sink;
using rp::graph::test::Source;

namespace {

// rp.test.shape `x` with these settings (and document param values), then the case's other nodes and wires.
std::string doc(const std::string& shape, const std::string& nodes = "", const std::string& wires = "",
                const std::string& params = "") {
    return R"({"nodes":[{"id":"x","type":"rp.test.shape","settings":)" + shape +
           (params.empty() ? "" : R"(,"params":)" + params) + "}" + (nodes.empty() ? "" : "," + nodes) +
           R"(],"wires":[)" + wires + "]}";
}

// rp.test.source whose prepare answers swap whatever changed, which for a change to its layout breaks the contract.
rp_prepare_result alwaysSwap(rp_node*, const rp_settings*, void** prepared, rp_error*) {
    *prepared = new test::SourceSettings{};
    return RP_PREPARE_SWAP;
}
const char* swapperDescriptor() { return R"({"id":"rp.test.swapper"})"; }
rp_node* swapperCreate(const rp_host* host, void* memory, const rp_settings* s, rp_error* err) {
    rp_node* n = rp::node_type<Source>()->create(host, memory, s, err);
    if (!n) return nullptr;
    static const rp_node_vtable vt = [&] {
        rp_node_vtable v = *n->vt;
        v.prepare_settings = &alwaysSwap;
        return v;
    }();
    n->vt = &vt;
    return n;
}

const rp_package* reshapePackage() {
    static const rp_node_type swapper{"rp.test.swapper", &swapperDescriptor, rp::node_type<Source>()->layout_for,
                                      rp::node_type<Source>()->query_memory, &swapperCreate,
                                      rp::node_type<Source>()->destroy, 0u};
    static const rp_node_type* const kTypes[] = {&swapper};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.reshape"})", 1, kTypes};
    return &kPackage;
}

// The tag of x's settings as each host command was applied.
struct TagLog {
    rp_node*          x = nullptr;
    std::vector<char> tags;
};

void logTag(void* ctx, const HostCommand&, const void*, bool) {
    auto* log = static_cast<TagLog*>(ctx);
    log->tags.push_back(rp::peer_as<Shape>(log->x)->settings->tag[0]);
}

}  // namespace

TEST_CASE("a reshape keeps the instance and its state through a layout change", "[graph][reshape]") {
    GraphRig rig;
    const std::string sink = R"({"id":"s","type":"rp.test.sink"})";
    const std::string wire = R"({"from":"x.b","to":"s.events"})";
    REQUIRE(rig.set(doc(R"({"outs":["b"]})", sink, wire)).ok);
    rig.run(64, 3);
    rp_node* const before = rig.host->node("x");
    REQUIRE(rig.set(doc(R"({"outs":["a","b"]})", sink, wire)).ok);
    CHECK(rig.host->node("x") == before);
    rig.run();
    const Shape& x = rig.node<Shape>("x");
    CHECK(x.blocks == 4);
    CHECK(x.ranOutputs == 2);

    // A reshape that leaves the layout as it was is a swap: the instance takes the settings and nothing else.
    const std::uint32_t maskCalls = x.maskCalls;
    REQUIRE(rig.set(doc(R"({"outs":["a","b"],"tag":"c"})", sink, wire)).ok);
    rig.run();
    CHECK(rig.host->node("x") == before);
    CHECK(x.ranWith == 'c');
    CHECK(x.blocks == 5);
    CHECK(x.maskCalls == maskCalls);
}

TEST_CASE("a reshape rebinds ports, params and control outputs by id", "[graph][reshape]") {
    GraphRig rig;
    const std::string nodes = R"({"id":"src","type":"rp.test.source"},{"id":"s","type":"rp.test.sink"},)"
                              R"({"id":"ls","type":"rp.test.sink"})";
    const std::string wires = R"({"from":"x.b","to":"s.events"},{"from":"src.events","to":"x.q"},)"
                              R"({"from":"x.L","to":"ls.control"})";
    const auto shape = [&](const char* settings) { return rig.set(doc(settings, nodes, wires)); };
    // What survives every reshape below: b still reaches s, q still hears src, h keeps its value, L its 5.
    const auto survived = [&] {
        const Shape& x = rig.node<Shape>("x");
        const Sink& s = rig.node<Sink>("s");
        CHECK(s.eventCount == 1);
        CHECK(s.eventBytes[0] == 'b');
        CHECK(x.received['q'] == 1);
        CHECK(x.knobs['h'] == 42.0);
        CHECK(rig.node<Sink>("ls").controlStart == 5.0);  // set on x's first block only, then held by the stream
    };
    REQUIRE(shape(R"({"ins":["q"],"outs":["b"],"levels":["L"],"knobs":["h"]})").ok);
    REQUIRE(rig.host->setParam("x", "h", 42.0));
    rig.run(64, 2);
    survived();

    // One of each added before it...
    REQUIRE(shape(R"({"ins":["p","q"],"outs":["a","b"],"levels":["K","L"],"knobs":["g","h"]})").ok);
    rig.run();
    survived();
    CHECK(rig.node<Shape>("x").received['p'] == 0);
    CHECK(rig.node<Shape>("x").knobs['g'] == 0.0);  // a new param starts at its default

    // ...then those removed, and others added after it.
    REQUIRE(shape(R"({"ins":["q","r"],"outs":["b","c"],"levels":["L","M"],"knobs":["h","i"]})").ok);
    rig.run();
    survived();
    CHECK_FALSE(rig.host->setParam("x", "g", 1.0));  // gone with the layout that had it
    REQUIRE(rig.host->setParam("x", "i", 7.0));
    rig.run(64, 2);
    CHECK(rig.node<Shape>("x").knobs['i'] == 7.0);
    CHECK(rig.node<Shape>("x").blocks == 6);
}

TEST_CASE("a reshape sends document values to the params it adds, not again to the ones it keeps", "[graph][reshape]") {
    GraphRig rig;
    REQUIRE(rig.set(doc(R"({"knobs":["h"]})", "", "", R"({"h":30})")).ok);
    rig.run();
    const Shape& x = rig.node<Shape>("x");
    CHECK(x.knobs['h'] == 30.0);
    REQUIRE(rig.host->setParam("x", "h", 50.0));
    rig.run(64, 2);
    REQUIRE(x.knobs['h'] == 50.0);

    REQUIRE(rig.set(doc(R"({"knobs":["g","h"]})", "", "", R"({"g":7,"h":30})")).ok);
    rig.run();
    CHECK(x.knobs['g'] == 7.0);
    CHECK(x.knobs['h'] == 50.0);  // h's document value was already sent; the live value stands

    // A param dropped and then added back is a new param, so its document value is sent again.
    REQUIRE(rig.set(doc(R"({"knobs":["h"]})", "", "", R"({"h":30})")).ok);
    REQUIRE(rig.set(doc(R"({"knobs":["g","h"]})", "", "", R"({"g":7,"h":30})")).ok);
    rig.run();
    CHECK(x.knobs['g'] == 7.0);
}

TEST_CASE("a feedback wire into or out of a reshaped node keeps its block in flight", "[graph][reshape][feedback]") {
    GraphRig rig;
    const std::string nodes = R"({"id":"src","type":"rp.test.source"},{"id":"s","type":"rp.test.sink"})";
    const std::string wires = R"({"from":"x.b","to":"s.events","feedback":true},)"
                              R"({"from":"src.events","to":"x.q","feedback":true})";
    REQUIRE(rig.set(doc(R"({"ins":["q"],"outs":["b"]})", nodes, wires)).ok);
    rig.run(64, 2);
    rp_node* const before = rig.host->node("x");
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(doc(R"({"ins":["p","q"],"outs":["a","b"]})", nodes, wires)).ok);
    // The old plan's last block (run with no drain) sends into both wires; the new plan's first block, which
    // adopts the reshape, reads what it sent.
    rig.host->run(64, rig.transport);
    rig.run();
    CHECK(rig.host->node("x") == before);
    CHECK(rig.node<Shape>("x").ranOutputs == 2);
    CHECK(rig.node<Shape>("x").received['q'] == 1);
    CHECK(rig.node<Sink>("s").eventCount == 1);
    CHECK(rig.node<Sink>("s").eventBytes[0] == 'b');
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("a reshape's settings commit at the swap, in push order with every other command", "[graph][reshape]") {
    GraphRig rig;
    REQUIRE(rig.set(doc(R"({"outs":["b"],"knobs":["g"]})")).ok);
    TagLog log{rig.host->node("x"), {}};
    rig.host->setHostCommandHandler(&logTag, &log);
    rig.run();
    rig.host->setAudioThreadOwns(true);
    const char c = 0;
    REQUIRE(rig.host->pushHostCommand(&c, 1));
    REQUIRE(rig.host->setParam("x", "g", 1.0));  // to a param the reshape drops: it reaches the stream it was sent to
    REQUIRE(rig.set(doc(R"({"outs":["a","b"],"tag":"b"})")).ok);
    REQUIRE(rig.host->pushHostCommand(&c, 1));
    const Shape& x = rig.node<Shape>("x");
    rig.host->run(64, rig.transport);  // the old plan's last block: nothing has been committed yet
    CHECK(x.ranWith == 'a');
    CHECK(x.ranOutputs == 1);
    CHECK(x.commits == 1);  // create's
    CHECK(log.tags.empty());
    rig.run();  // the drain (a command, the swap and its commit, a command), then the new plan's first block
    CHECK(log.tags == std::vector<char>{'a', 'b'});
    CHECK(x.commits == 2);
    CHECK(x.committed[1] == 'b');
    CHECK(x.ranWith == 'b');
    CHECK(x.ranOutputs == 2);
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("a reshaped node is told its connected outputs at the swap, by its new layout", "[graph][reshape]") {
    GraphRig rig;
    const std::string sink = R"({"id":"s","type":"rp.test.sink"})";
    const std::string wire = R"({"from":"x.b","to":"s.events"})";
    REQUIRE(rig.set(doc(R"({"outs":["a","b"]})", sink, wire)).ok);
    rig.run();
    const Shape& x = rig.node<Shape>("x");
    REQUIRE(x.maskCalls == 1);
    CHECK(x.maskSize == 2);
    CHECK((x.mask[0] == 0 && x.mask[1] == 1));

    // b moves to the front: connected as before by id, but not by position.
    REQUIRE(rig.set(doc(R"({"outs":["b","c","d"],"tag":"b"})", sink, wire)).ok);
    rig.run();
    CHECK(x.maskCalls == 2);
    CHECK(x.maskSize == 3);
    CHECK((x.mask[0] == 1 && x.mask[1] == 0 && x.mask[2] == 0));
    CHECK(x.maskedWith == 'b');  // after the commit: the mask is read against the settings it belongs to

    // Only an input added: the same mask by id and by position, and a reshape still brings one.
    REQUIRE(rig.set(doc(R"({"ins":["q"],"outs":["b","c","d"],"tag":"c"})", sink, wire)).ok);
    rig.run();
    CHECK(x.maskCalls == 3);
    CHECK(x.maskSize == 3);
    CHECK(x.maskedWith == 'c');
}

TEST_CASE("a node that answers rebuild for a layout change is still rebuilt", "[graph][reshape]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":2}}],"wires":[]})").ok);
    rig.run(64, 2);
    rp_node* const before = rig.host->node("src");
    REQUIRE(rig.node<Source>("src").frames == 128);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":1}}],"wires":[]})").ok);
    CHECK(rig.host->node("src") != before);
    CHECK(rig.node<Source>("src").frames == 0);
}

TEST_CASE("a node that answers swap for a change to its layout breaks the contract", "[graph][reshape]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(reshapePackage()));
    REQUIRE(rig.set(R"({"nodes":[{"id":"w","type":"rp.test.swapper","settings":{"channels":2}}],"wires":[]})").ok);
    rp_node* const before = rig.host->node("w");
    // What it prepared is released, not leaked (the ASan build checks).
    const auto r = rig.set(R"({"nodes":[{"id":"w","type":"rp.test.swapper","settings":{"channels":1}}],"wires":[]})");
    REQUIRE(GraphRig::codes(r) == "node-contract");
    CHECK(rig.host->node("w") == before);
    CHECK(rig.set(R"({"nodes":[{"id":"w","type":"rp.test.swapper","settings":{"channels":2,"value":0.5}}],)"
                  R"("wires":[]})").ok);  // a change it may swap
}

TEST_CASE("setGraph never waits on a stateless prepare's commits, and on every other as before",
          "[graph][reshape][swap]") {
    GraphRig rig({48000.0, 64, Target::Desktop, 50});  // a 50 ms adoption timeout
    const auto both = [](const char* shape, const char* value) {
        return doc(shape, std::string(R"({"id":"src","type":"rp.test.source","settings":{"value":)") + value + "}}");
    };
    REQUIRE(rig.set(both(R"({"outs":["b"]})", "1")).ok);
    rig.run();
    rig.host->setAudioThreadOwns(true);  // and no block runs until the end, so nothing is adopted
    // Two reshapes committing settings to x, the second planned while the first is unadopted.
    REQUIRE(rig.set(both(R"({"outs":["a","b"],"tag":"b"})", "1")).ok);
    REQUIRE(rig.set(both(R"({"outs":["b"],"tag":"c"})", "1")).ok);
    // A commit to src, whose prepare reads its settings: nothing in flight makes this one wait...
    REQUIRE(rig.set(both(R"({"outs":["b"],"tag":"c"})", "0.5")).ok);
    // ...and it makes the next one wait, and refuse at the timeout, exactly as before.
    CHECK(GraphRig::codes(rig.set(both(R"({"outs":["b"],"tag":"d"})", "0.5"))) == "busy");
    rig.run();  // adopts all three, in order
    const Shape& x = rig.node<Shape>("x");
    CHECK(x.commits == 3);
    CHECK(x.committed[1] == 'b');
    CHECK(x.committed[2] == 'c');
    CHECK(x.ranWith == 'c');
    CHECK(x.ranOutputs == 1);
    CHECK(rig.node<Source>("src").value == 0.5f);
    CHECK(rig.set(both(R"({"outs":["b"],"tag":"d"})", "0.5")).ok);
    rig.run();
    CHECK(x.ranWith == 'd');
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("a reshape in a document refused later leaves the instance and its layout as they were", "[graph][reshape]") {
    GraphRig rig;
    const std::string sink = R"({"id":"s","type":"rp.test.sink"})";
    const std::string wire = R"({"from":"x.b","to":"s.events"})";
    REQUIRE(rig.set(doc(R"({"outs":["b"],"knobs":["h"]})", sink, wire)).ok);
    rig.run();
    rp_node* const before = rig.host->node("x");
    const Shape& x = rig.node<Shape>("x");
    const std::uint32_t maskCalls = x.maskCalls;
    // Reconcile prepares the reshape; only the coupled-group check, after it, refuses the document.
    const auto r = rig.set(doc(R"({"outs":["a","b"],"knobs":["g"],"tag":"b"})",
                               sink + R"(,{"id":"l","type":"rp.test.linked"},{"id":"n","type":"rp.test.nostep"})",
                               wire + R"(,{"from":"l.link","to":"n.link"})"));
    REQUIRE(GraphRig::codes(r) == "link-without-step");
    CHECK(rig.host->node("x") == before);
    CHECK(rig.host->setParam("x", "h", 3.0));        // the old layout's param...
    CHECK_FALSE(rig.host->setParam("x", "g", 3.0));  // ...and not the refused one's
    rig.run();
    CHECK(x.commits == 1);
    CHECK(x.ranWith == 'a');
    CHECK(x.ranOutputs == 1);
    CHECK(x.maskCalls == maskCalls);
    CHECK(rig.node<Sink>("s").eventBytes[0] == 'b');

    // The same reshape on its own still lands.
    REQUIRE(rig.set(doc(R"({"outs":["a","b"],"knobs":["g"],"tag":"b"})", sink, wire)).ok);
    rig.run();
    CHECK(rig.host->node("x") == before);
    CHECK(x.ranWith == 'b');
    CHECK(x.ranOutputs == 2);
}
