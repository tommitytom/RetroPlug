// A real audio thread runs blocks while the control thread swaps plans, reshapes a node, sets params and
// reclaims. Under ThreadSanitizer (build-tsan/) this is the race check for the command ring, the release ring
// and a reshape's new streams; in a normal build it checks that a system's own output and state survive
// constant edits around it. The adoption handshake itself (setGraph waiting only while a published plan still
// carries a settings commit) has its own deterministic tests below (it waits while a commit is queued, and
// while one is being adopted), since a race window that narrow is not something a stress run can be relied
// on to hit.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "GraphRig.hpp"

using rp::graph::GraphHost;
using rp::graph::test::GraphRig;
using rp::graph::test::Shape;
using rp::graph::test::Sink;
using rp::graph::test::Source;

namespace {

// A node whose settings swap live for a level change and rebuild for a tag change.
struct TunableSettings {
    double      level = 0.5;
    std::string tag = "a";
};

struct Tunable {
    static constexpr const char* id = "rp.test.tunable";
    using Settings = TunableSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;
    static bool needs_rebuild(const Settings& a, const Settings& b) { return a.tag != b.tag; }
    float level = 0.5f;
    void on_settings(const Settings& s) noexcept { level = static_cast<float>(s.level); }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = level;
    }
};

const rp_package* stressPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Tunable>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.stress"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

// Runs blocks on its own thread until destroyed; stopping and joining in the destructor means a failed
// check on the control thread unwinds cleanly instead of destroying a joinable std::thread.
class AudioThread {
public:
    explicit AudioThread(GraphHost& host) : host_(host), thread_([this] { run(); }) {}
    ~AudioThread() { stop(); }
    // Stops and joins, then hands the drain back to the control thread.
    void stop() {
        halt();
        host_.setAudioThreadOwns(false);
    }
    // Stops and joins but keeps the drain with the (now stopped) audio thread, so pushes queue unapplied.
    void halt() {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }
    int  blocks() const { return blocks_.load(); }
    bool wrong() const { return wrong_.load(); }
    // Wait (up to a deadline) until `n` more blocks have run.
    bool waitBlocks(int n) const {
        const int target = blocks() + n;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (blocks() < target)
            if (std::chrono::steady_clock::now() > deadline) return false;
            else std::this_thread::yield();
        return true;
    }

private:
    void run() {
        std::vector<std::vector<float>> outs(8, std::vector<float>(64));
        std::vector<float*> ptrs;
        for (auto& o : outs) ptrs.push_back(o.data());
        const rp_transport t{48000.0, 120.0, 0.0, 0};
        while (running_.load(std::memory_order_acquire)) {
            host_.processBlock(64, t, ptrs.data(), 8);
            // The source is on one pair or the other, every frame of the block.
            const bool onFirst = outs[0][0] == 0.5f && outs[1][63] == 0.5f;
            const bool onSecond = outs[2][0] == 0.5f && outs[3][63] == 0.5f;
            if (!onFirst && !onSecond) wrong_ = true;
            blocks_.fetch_add(1);
        }
    }

    GraphHost&        host_;
    std::atomic<bool> running_{true};
    std::atomic<int>  blocks_{0};
    std::atomic<bool> wrong_{false};
    std::thread       thread_;  // last: it starts running in the constructor
};

// Variants differ in unrelated nodes, the source's output pair, a live settings swap (every variant) and a
// rebuild (every fifth), feedback wires added and removed, and a resource binding's loop setting.
std::string stressDoc(int variant, rp::graph::ResourceTable::Id buf) {
    std::string extra;
    for (int i = 0; i < variant % 4; ++i) extra += R"(,{"id":"x)" + std::to_string(i) + R"(","type":"rp.test.sink"})";
    const std::string pair = variant % 2 ? "out.3-4" : "out.1-2";
    std::string wires = R"({"from":"src.out","to":"out.)" + pair + R"("},{"from":"tun.out","to":"ts.in"},)"
                        R"({"from":"p.out","to":"ps.in"},{"from":"k.out","to":"ks.control"})";
    if (variant % 3)
        wires += R"(,{"from":"src.events","to":"fbs.events","feedback":true},{"from":"src.out","to":"fba.in","feedback":true})";
    return R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},{"id":"out","type":"rp.host.audio-out"},)"
           R"({"id":"tun","type":"rp.test.tunable","settings":{"level":)" + std::to_string(variant) + R"(,"tag":"t)" +
           std::to_string(variant / 5) + R"("}},)"
           R"({"id":"ts","type":"rp.test.sink"},{"id":"fbs","type":"rp.test.sink"},{"id":"fba","type":"rp.test.through"},)"
           R"({"id":"ps","type":"rp.test.sink"},{"id":"k","type":"rp.core.constant","settings":{"max":1000}},)"
           R"({"id":"ks","type":"rp.test.sink"},)"
           R"({"id":"p","type":"rp.core.buffer-player","settings":{"loop":)" + std::string(variant % 2 ? "true" : "false") +
           R"(},"resources":{"sample":"res:)" + std::to_string(buf) + R"("}})" + extra + R"(],"wires":[)" + wires + "]}";
}

}  // namespace

TEST_CASE("plan swaps under a running audio thread keep a node's output and state", "[graph][swap][stress]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(stressPackage()));
    const auto buf = rig.resources.createAudioBuffer(2, 512, 48000.0);
    REQUIRE(rig.set(stressDoc(0, buf)).ok);
    rig.host->setAudioThreadOwns(true);

    std::string failures;  // checked as one string, so a failure prints every code
    bool settled = false;
    {
        AudioThread audio(*rig.host);
        for (int i = 1; i <= 200; ++i) {
            const auto r = rig.set(stressDoc(i, buf));
            if (!r.ok) failures += "set " + std::to_string(i) + ": " + GraphRig::codes(r) + "; ";
            if (!rig.host->setParam("k", "value", static_cast<double>(i))) failures += "param " + std::to_string(i) + "; ";
            rig.host->reclaim();
        }
        // The last param before one more swap, and that swap a live one (201 shares 200's tag): what is
        // checked below has to survive a swap, not merely be the newest thing queued.
        if (!rig.host->setParam("k", "value", 201.0)) failures += "param 201; ";
        if (!rig.set(stressDoc(201, buf)).ok) failures += "set 201; ";
        settled = audio.waitBlocks(4);
        audio.stop();
        CHECK(failures.empty());
        CHECK_FALSE(audio.wrong());
        CHECK(rig.node<Source>("src").frames == static_cast<std::uint64_t>(audio.blocks()) * 64);
    }

    CHECK(settled);
    CHECK(rig.node<Tunable>("tun").level == 201.0f);  // the last LIVE settings swap landed
    CHECK(rig.node<Sink>("ks").controlEnd == 201.0);  // and the param value survived it
}

namespace {

// Variants of a reshaping node: ports and a knob added and dropped before and after the wired ones, a new tag,
// and a feedback wire out of it on odd variants.
std::string reshapeDoc(int variant) {
    std::string outs = variant % 3 ? R"("a","b")" : R"("b")";
    if (variant % 2) outs += R"(,"c")";
    std::string wires = R"({"from":"src.out","to":"out.out.1-2"},{"from":"x.b","to":"s.events"},)"
                        R"({"from":"src.events","to":"x.q"},{"from":"x.L","to":"ls.control"})";
    if (variant % 2) wires += R"(,{"from":"x.b","to":"fs.events","feedback":true})";
    const std::string ins = variant % 5 ? R"(["p","q"])" : R"(["q"])";
    const std::string levels = variant % 4 ? R"(["L"])" : R"(["K","L"])";
    const std::string knobs = variant % 4 ? R"(["g","h"])" : R"(["h"])";
    return R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},)"
           R"({"id":"out","type":"rp.host.audio-out"},{"id":"x","type":"rp.test.shape","settings":{"ins":)" + ins +
           R"(,"outs":[)" + outs + R"(],"levels":)" + levels + R"(,"knobs":)" + knobs + R"(,"tag":")" +
           std::to_string(variant % 7) + R"("}},{"id":"s","type":"rp.test.sink"},{"id":"fs","type":"rp.test.sink"},)"
           R"({"id":"ls","type":"rp.test.sink"}],"wires":[)" + wires + "]}";
}

}  // namespace

TEST_CASE("reshapes under a running audio thread keep the instance, its state and its streams",
          "[graph][swap][stress][reshape]") {
    GraphRig rig;
    REQUIRE(rig.set(reshapeDoc(0)).ok);
    rp_node* const x = rig.host->node("x");
    rig.host->setAudioThreadOwns(true);

    std::string failures;
    bool settled = false;
    {
        AudioThread audio(*rig.host);
        // A stateless prepare's commits never make setGraph wait, so it can fill the ring faster than the audio
        // thread drains it: retry, it is draining.
        const auto retry = [&](auto&& attempt) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!attempt())
                if (std::chrono::steady_clock::now() > deadline) return false;
                else std::this_thread::yield();
            return true;
        };
        for (int i = 1; i <= 200; ++i) {
            if (!retry([&] { return rig.set(reshapeDoc(i)).ok; })) failures += "set " + std::to_string(i) + "; ";
            if (!retry([&] { return rig.host->setParam("x", "h", static_cast<double>(i % 100)); }))
                failures += "param " + std::to_string(i) + "; ";
            rig.host->reclaim();
        }
        if (!retry([&] { return rig.host->setParam("x", "h", 99.0); })) failures += "param 99; ";
        if (!retry([&] { return rig.set(reshapeDoc(201)).ok; })) failures += "set 201; ";
        settled = audio.waitBlocks(4);
        audio.stop();
        CHECK(failures.empty());
        CHECK_FALSE(audio.wrong());
        CHECK(rig.host->node("x") == x);  // never rebuilt...
        CHECK(rig.node<Shape>("x").blocks == static_cast<std::uint32_t>(audio.blocks()));  // ...and ran every block
    }

    CHECK(settled);
    CHECK(rig.node<Shape>("x").ranWith == '5');           // variant 201's tag
    CHECK(rig.node<Shape>("x").knobs['h'] == 99.0);       // h's stream survived every reshape
    CHECK(rig.node<Sink>("ls").controlStart == 5.0);      // and so did L's
    CHECK(rig.node<Sink>("s").eventBytes[0] == 'b');
}

TEST_CASE("setGraph plans again at once while the plans in flight carry no settings commit", "[graph][swap]") {
    GraphRig rig({48000.0, 64, rp::graph::Target::Desktop, 50});  // a 50 ms adoption timeout
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"}],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);
    // Nothing drains these, so all three sit in the ring at once; none commits settings, so none waits.
    const auto withData = [&](const char* json, int data) {
        return rig.host->setGraph(*rp::graph::parseGraphDocument(json), std::make_shared<const int>(data));
    };
    REQUIRE(withData(R"({"nodes":[{"id":"a","type":"rp.test.sink"},{"id":"b","type":"rp.test.sink"}],"wires":[]})", 1).ok);
    REQUIRE(withData(R"({"nodes":[{"id":"b","type":"rp.test.sink"}],"wires":[]})", 2).ok);
    REQUIRE(withData(R"({"nodes":[],"wires":[]})", 3).ok);
    CHECK(rig.host->hostData() == nullptr);  // nothing adopted yet
    rig.run();  // one drain adopts all three, in order, so the last is the one running
    REQUIRE(rig.host->hostData() != nullptr);
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 3);
    CHECK(rig.host->reclaim() == 3);  // the first plan and the two adopted in passing
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("setGraph waits while a published settings commit is unadopted", "[graph][swap]") {
    GraphRig rig({48000.0, 64, rp::graph::Target::Desktop, 50});
    REQUIRE(rig.registry.addPackage(stressPackage()));
    REQUIRE(rig.set(R"({"nodes":[{"id":"t","type":"rp.test.tunable","settings":{"level":1}}],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(R"({"nodes":[{"id":"t","type":"rp.test.tunable","settings":{"level":2}}],"wires":[]})").ok);  // a live swap
    // Its commit has not run, so the planner may not ask the node to prepare again.
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[],"wires":[]})")) == "busy");
    rig.run();  // the audio thread adopts it
    CHECK(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.run();
    rig.host->setAudioThreadOwns(false);
}

namespace {

// Holds the audio thread inside a settings commit for as long as `held` is set.
std::atomic<bool> held{false};
std::atomic<bool> holding{false};

struct GateSettings {
    bool hold = false;
};

struct Gate {
    static constexpr const char* id = "rp.test.gate";
    using Settings = GateSettings;
    void on_settings(const Settings& s) noexcept {
        if (!s.hold) return;
        holding = true;
        while (held.load()) std::this_thread::yield();
        holding = false;
    }
    void process(const rp::Block&) noexcept {}
};

const rp_package* gatePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Gate>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.gate"})", 1, kTypes};
    return &kPackage;
}

}  // namespace

TEST_CASE("setGraph also waits while a plan is being adopted", "[graph][swap]") {
    GraphRig rig({48000.0, 64, rp::graph::Target::Desktop, 50});
    REQUIRE(rig.registry.addPackage(gatePackage()));
    REQUIRE(rig.set(R"({"nodes":[{"id":"g","type":"rp.test.gate"}],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(R"({"nodes":[{"id":"g","type":"rp.test.gate","settings":{"hold":true}}],"wires":[]})").ok);  // a live swap
    held = true;
    std::thread audio([&] { rig.run(); });  // adopts it, and stops inside its commit
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!holding.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK(holding.load());
    // Mid-adoption: the planner would read the node's settings while the audio thread writes them.
    const auto busy = rig.set(R"({"nodes":[],"wires":[]})");
    held = false;
    audio.join();
    CHECK(GraphRig::codes(busy) == "busy");
    CHECK(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.run();
    rig.host->setAudioThreadOwns(false);
}

namespace {

// Each applied host command must see the host data of the plan it was pushed behind.
struct CommandAudit {
    std::atomic<int> applied{0};
    std::atomic<int> discarded{0};
    std::atomic<int> mismatched{0};
};

void audit(void* ctx, const rp::graph::HostCommand& c, const void* hostData, bool discard) {
    auto* a = static_cast<CommandAudit*>(ctx);
    int i = -1;
    std::memcpy(&i, c.bytes, sizeof i);
    if (!hostData || *static_cast<const int*>(hostData) != i) a->mismatched.fetch_add(1, std::memory_order_relaxed);
    (discard ? a->discarded : a->applied).fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

TEST_CASE("host commands under a running audio thread each see their own plan's host data", "[graph][swap][stress]") {
    GraphRig rig;
    CommandAudit counts;
    rig.host->setHostCommandHandler(&audit, &counts);
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);

    constexpr int kRounds = 2500;
    constexpr int kTail   = 8;  // pushed after the audio thread halts, so teardown discards them
    int           pushed = 0;
    std::vector<std::weak_ptr<const int>> data;  // each plan's host data, freed with the plan
    {
        AudioThread audio(*rig.host);
        // The ring can be momentarily full while the audio thread is between blocks: retry, it is draining.
        const auto retry = [&](auto&& attempt) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!attempt())
                if (std::chrono::steady_clock::now() > deadline) return false;
                else std::this_thread::yield();
            return true;
        };
        for (int i = 0; i < kRounds; ++i) {
            const auto doc = *rp::graph::parseGraphDocument(i % 2 ? R"({"nodes":[{"id":"s","type":"rp.test.sink"}],"wires":[]})"
                                                                    : R"({"nodes":[],"wires":[]})");
            auto hostData = std::make_shared<const int>(i);
            data.push_back(hostData);
            if (!retry([&] { return rig.host->setGraph(doc, hostData).ok; })) break;
            if (!retry([&] { return rig.host->pushHostCommand(&i, sizeof i); })) break;
            ++pushed;
            hostData.reset();
            if (i % 16 == 0) rig.host->reclaim();
        }
        audio.halt();
        for (int i = kRounds; i < kRounds + kTail; ++i) {
            auto hostData = std::make_shared<const int>(i);
            data.push_back(hostData);
            if (!rig.host->setGraph(*rp::graph::parseGraphDocument(R"({"nodes":[],"wires":[]})"), hostData).ok) break;
            if (!rig.host->pushHostCommand(&i, sizeof i)) break;
            ++pushed;
        }
        rig.host->discardPending();  // what the halted audio thread never reached
        rig.host->setAudioThreadOwns(false);
    }
    rig.host->reclaim();

    CHECK(pushed == kRounds + kTail);
    CHECK(counts.mismatched.load() == 0);
    CHECK(counts.discarded.load() >= kTail);  // plus any the audio thread had not reached when it halted
    CHECK(counts.applied.load() + counts.discarded.load() == pushed);
    CHECK(rig.host->leakedPlans() == 0);
    // Every plan but the one still running has been destroyed, and its host data with it.
    int alive = 0;
    for (const auto& d : data) alive += d.expired() ? 0 : 1;
    CHECK(alive == 1);
}
