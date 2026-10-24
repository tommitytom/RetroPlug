// The embedder's half of the one ring: host commands keep their push order with plan swaps and params,
// each seeing the host data of the plan current when it is applied; discardPending, at teardown, adopts
// the queued plans and hands the queued commands back as discarded; and a document gets at most one
// rp.host.audio-out.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

namespace {

struct Seen {
    char cmd;
    int  data;  // the plan's host data, or -1 for none
    bool discard;
};

void record(void* ctx, const HostCommand& c, const void* hostData, bool discard) {
    static_cast<std::vector<Seen>*>(ctx)->push_back(
        {static_cast<char>(c.bytes[0]), hostData ? *static_cast<const int*>(hostData) : -1, discard});
}

SetGraphResult setWith(GraphRig& rig, const char* json, int data) {
    return rig.host->setGraph(*parseGraphDocument(json), std::make_shared<const int>(data));
}

bool push(GraphHost& h, char c) { return h.pushHostCommand(&c, 1); }

}  // namespace

TEST_CASE("host commands keep push order with plan swaps, each seeing its plan's host data", "[graph][host-commands]") {
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    REQUIRE(setWith(rig, R"({"nodes":[],"wires":[]})", 1).ok);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(push(*rig.host, 'a'));
    REQUIRE(setWith(rig, R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})", 2).ok);
    REQUIRE(push(*rig.host, 'b'));
    REQUIRE(setWith(rig, R"({"nodes":[],"wires":[]})", 3).ok);
    REQUIRE(push(*rig.host, 'c'));
    CHECK(seen.empty());  // push-only while the audio thread owns the host
    rig.host->drain();    // two swaps and three commands in one drain
    REQUIRE(seen.size() == 3);
    CHECK((seen[0].cmd == 'a' && seen[0].data == 1));
    CHECK((seen[1].cmd == 'b' && seen[1].data == 2));
    CHECK((seen[2].cmd == 'c' && seen[2].data == 3));
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 3);
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("while quiescent a host command applies as it is pushed", "[graph][host-commands]") {
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    REQUIRE(push(*rig.host, 'x'));
    REQUIRE(seen.size() == 1);
    CHECK((seen[0].cmd == 'x' && seen[0].data == -1 && !seen[0].discard));
    const std::vector<std::byte> big(kHostCommandBytes + 1);
    CHECK_FALSE(rig.host->pushHostCommand(big.data(), big.size()));
}

TEST_CASE("discardPending adopts queued plans and discards queued host commands", "[graph][host-commands]") {
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(setWith(rig, R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})", 7).ok);
    REQUIRE(push(*rig.host, 'r'));
    rig.host->discardPending();  // teardown, with the audio thread stopped
    rig.host->setAudioThreadOwns(false);
    REQUIRE(seen.size() == 1);
    CHECK((seen[0].cmd == 'r' && seen[0].data == 7 && seen[0].discard));
    CHECK(rig.host->node("s") != nullptr);
}

TEST_CASE("a document gets at most one rp.host.audio-out", "[graph][host-commands]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"o1","type":"rp.host.audio-out"},{"id":"o2","type":"rp.host.audio-out"}],"wires":[]})");
    CHECK(GraphRig::codes(r) == "audio-out-twice");
}
