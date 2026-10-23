// A real audio thread runs blocks while the control thread swaps plans, sets params and reclaims. Under
// ThreadSanitizer (build-tsan/) this is the race check for the command ring, the release ring and plan
// adoption; in a normal build it checks that a system's own output survives constant unrelated edits.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "GraphRig.hpp"

using rp::graph::test::GraphRig;
using rp::graph::test::Source;

TEST_CASE("plan swaps under a running audio thread keep a node's output and state", "[graph][swap][stress]") {
    GraphRig rig;
    auto doc = [](int variant) {
        std::string extra;
        for (int i = 0; i < variant % 4; ++i) extra += R"(,{"id":"x)" + std::to_string(i) + R"(","type":"rp.test.sink"})";
        const std::string pair = variant % 2 ? "out.3-4" : "out.1-2";
        return R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},{"id":"out","type":"rp.host.audio-out"})" +
               extra + R"(],"wires":[{"from":"src.out","to":"out.)" + pair + R"("}]})";
    };
    REQUIRE(rig.set(doc(0)).ok);
    rig.host->setAudioThreadOwns(true);

    std::atomic<bool> running{true};
    std::atomic<int>  blocks{0};
    std::atomic<bool> wrong{false};
    std::thread audio([&] {
        std::vector<std::vector<float>> outs(8, std::vector<float>(64));
        std::vector<float*> ptrs;
        for (auto& o : outs) ptrs.push_back(o.data());
        rp_transport t{48000.0, 120.0, 0.0, 0};
        while (running.load(std::memory_order_acquire)) {
            for (auto& o : outs) std::fill(o.begin(), o.end(), 0.0f);
            rig.host->processBlock(64, t, ptrs.data(), 8);
            if (outs[0][0] != 0.5f && outs[2][0] != 0.5f) wrong = true;
            blocks.fetch_add(1);
        }
    });

    for (int i = 1; i <= 200; ++i) {
        REQUIRE(rig.set(doc(i)).ok);
        rig.host->setParam("src", "gain", 1.0);
        rig.host->reclaim();
    }
    // Let the last swap land before stopping the thread.
    const int seen = blocks.load();
    while (blocks.load() < seen + 4) std::this_thread::yield();
    running = false;
    audio.join();
    rig.host->setAudioThreadOwns(false);

    CHECK_FALSE(wrong.load());
    CHECK(rig.node<Source>("src").frames == static_cast<std::uint64_t>(blocks.load()) * 64);
}
