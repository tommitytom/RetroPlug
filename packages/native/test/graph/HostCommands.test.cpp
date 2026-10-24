// The embedder's half of the one ring: host commands keep their push order with plan swaps and params,
// each seeing the host data of the plan current when it is applied; a plan's trailer rides in its swap, so
// it lands with the plan or not at all; discardPending, at teardown, adopts the queued plans (their
// trailers applied) and hands the queued commands back as discarded; and a document gets at most one
// rp.host.audio-out.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

SetGraphResult setWith(GraphRig& rig, const char* json, int data, const HostCommand* trailer = nullptr) {
    return rig.host->setGraph(*parseGraphDocument(json), std::make_shared<const int>(data), trailer);
}

bool push(GraphHost& h, char c) { return h.pushHostCommand(&c, 1); }

HostCommand command(char c) {
    HostCommand h{};
    h.bytes[0] = static_cast<std::byte>(c);
    return h;
}

constexpr const char* kEmpty = R"({"nodes":[],"wires":[]})";
constexpr const char* kSink = R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})";

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
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 7);  // the queued plan was adopted
}

TEST_CASE("a plan's trailer is applied in the pop that adopts the plan, with that plan's host data",
          "[graph][host-commands][trailer]") {
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    REQUIRE(setWith(rig, kEmpty, 1).ok);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(push(*rig.host, 'a'));
    const HostCommand t = command('T'), u = command('U');
    REQUIRE(setWith(rig, kSink, 2, &t).ok);
    REQUIRE(setWith(rig, kEmpty, 3, &u).ok);  // a second swap in the same drain, right behind the first
    REQUIRE(push(*rig.host, 'b'));
    CHECK(seen.empty());
    rig.host->drain();
    // Each trailer sees its own plan: never the one before it, and never the one swapped in after it.
    REQUIRE(seen.size() == 4);
    CHECK((seen[0].cmd == 'a' && seen[0].data == 1 && !seen[0].discard));
    CHECK((seen[1].cmd == 'T' && seen[1].data == 2 && !seen[1].discard));
    CHECK((seen[2].cmd == 'U' && seen[2].data == 3 && !seen[2].discard));
    CHECK((seen[3].cmd == 'b' && seen[3].data == 3 && !seen[3].discard));
    rig.run(64, 2);  // and once only
    CHECK(seen.size() == 4);
    rig.host->setAudioThreadOwns(false);
    CHECK(seen.size() == 4);
}

TEST_CASE("a trailer takes no slot of its own, and a setGraph the ring refuses applies neither half",
          "[graph][host-commands][trailer]") {
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    REQUIRE(setWith(rig, kEmpty, 1).ok);
    rig.host->setAudioThreadOwns(true);
    std::size_t depth = 0;
    while (push(*rig.host, 'f')) ++depth;
    REQUIRE(depth > 1);
    const HostCommand t = command('T');

    // A full ring refuses the plan, and its trailer with it.
    CHECK(GraphRig::codes(setWith(rig, kSink, 2, &t)) == "busy");
    rig.host->drain();
    REQUIRE(seen.size() == depth);
    CHECK(std::all_of(seen.begin(), seen.end(), [](const Seen& s) { return s.cmd == 'f' && s.data == 1; }));
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 1);

    // One slot left: the plan and its trailer take it together, where a trailer pushed on its own would
    // have been dropped behind an applied plan.
    seen.clear();
    for (std::size_t i = 0; i + 1 < depth; ++i) REQUIRE(push(*rig.host, 'g'));
    REQUIRE(setWith(rig, kSink, 3, &t).ok);
    CHECK_FALSE(push(*rig.host, 'h'));  // full again
    rig.host->drain();
    REQUIRE(seen.size() == depth);
    CHECK((seen.back().cmd == 'T' && seen.back().data == 3));
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 3);
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("discardPending applies a queued plan's trailer with the plan", "[graph][host-commands][trailer]") {
    // The plan is adopted either way, and its trailer is the rest of the same edit, so it is applied, not
    // handed back as discarded; the commands queued around it still are.
    GraphRig rig;
    std::vector<Seen> seen;
    rig.host->setHostCommandHandler(&record, &seen);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(push(*rig.host, 'q'));
    const HostCommand t = command('T');
    REQUIRE(setWith(rig, kSink, 7, &t).ok);
    REQUIRE(push(*rig.host, 'r'));
    rig.host->discardPending();  // teardown, with the audio thread stopped
    rig.host->setAudioThreadOwns(false);
    REQUIRE(seen.size() == 3);
    CHECK((seen[0].cmd == 'q' && seen[0].data == -1 && seen[0].discard));
    CHECK((seen[1].cmd == 'T' && seen[1].data == 7 && !seen[1].discard));
    CHECK((seen[2].cmd == 'r' && seen[2].data == 7 && seen[2].discard));
}

TEST_CASE("a document gets at most one rp.host.audio-out", "[graph][host-commands]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"o1","type":"rp.host.audio-out"},{"id":"o2","type":"rp.host.audio-out"}],"wires":[]})");
    CHECK(GraphRig::codes(r) == "audio-out-twice");
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].nodeId == "o2");  // the second one is the one refused
}

TEST_CASE("plans queued in one drain all come back through the release ring", "[graph][host-commands]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);
    // A sleeping plugin keeps publishing; none carries a settings commit, so none waits. More than the old
    // 64-deep release ring, fewer than the command ring holds.
    constexpr int kPlans = 100;
    for (int i = 0; i < kPlans; ++i) REQUIRE(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})").ok);
    rig.run();  // one drain adopts them all
    CHECK(rig.host->leakedPlans() == 0);
    CHECK(rig.host->reclaim() == static_cast<std::size_t>(kPlans));  // the first plan and all but the last queued
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("re-activation keeps the plan's host data", "[graph][host-commands][swap]") {
    GraphRig rig;
    REQUIRE(setWith(rig, R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})", 5).ok);
    rig.run(64, 1);
    const void* before = rig.host->hostData();
    REQUIRE(before != nullptr);
    REQUIRE(rig.host->reactivate(96000.0, 64));
    rig.run(64, 1);
    CHECK(rig.host->hostData() == before);
    REQUIRE(rig.host->hostData() != nullptr);
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 5);
}
