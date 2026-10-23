// A real audio thread runs blocks while the control thread swaps plans, sets params and reclaims. Under
// ThreadSanitizer (build-tsan/) this is the race check for the command ring and the release ring; in a
// normal build it checks that a system's own output and state survive constant edits around it. The
// adoption handshake itself (setGraph never planning while a swap is in flight) has its own deterministic
// tests below (setGraph waits while a swap is queued, and while one is being adopted), since a race window
// that narrow is not something a stress run can be relied on to hit.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "GraphRig.hpp"

using rp::graph::GraphHost;
using rp::graph::test::GraphRig;
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
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
        host_.setAudioThreadOwns(false);
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

TEST_CASE("setGraph waits for the audio thread to adopt the last plan before planning again", "[graph][swap]") {
    GraphRig rig({48000.0, 64, rp::graph::Target::Desktop, 50});  // a 50 ms adoption timeout
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"}],"wires":[]})").ok);
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"},{"id":"b","type":"rp.test.sink"}],"wires":[]})").ok);
    // Nothing has drained the swap, so the next plan must not be made: the last one is still in flight.
    const auto busy = rig.set(R"({"nodes":[],"wires":[]})");
    CHECK(GraphRig::codes(busy) == "busy");
    rig.run();  // the audio thread's next block adopts it
    CHECK(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.run();
    CHECK(rig.host->reclaim() >= 1);  // the last plan was adopted: the one it replaced came back
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
