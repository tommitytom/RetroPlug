// Guards the per-channel host seam: a system that reports a wider channelLayout() is fanned out by the
// runner into one stereo lane-pair per stream, driven by the router's streamCount — while the default
// single-stream path stays a plain stereo (2-lane) finish. No emulator cores involved; a fake SystemBase
// stands in and writes a distinct marker into each lane so the routing can be asserted directly.
//
// Run via `pnpm test:plugin`.

#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/BlockRunner.hpp"
#include "system/SystemBase.hpp"

namespace {

// A minimal SystemBase double: reports `streams` stereo streams and, in finishBlock, SUMS a per-lane
// marker (lane index + 1) into every lane it's handed — so lane L holds (L+1) iff it was routed and
// written. It never touches emulator state; the triad's prepare/step defaults run it to done immediately.
class FakeSystem final : public SystemBase {
public:
    FakeSystem(SystemId id, int streams) : SystemBase(id), streams_(streams) {}

    SystemKind kind() const override { return SystemKind::SameBoy; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}

    std::vector<ChannelStream> channelLayout() const override {
        std::vector<ChannelStream> layout;
        for (int i = 0; i < streams_; ++i) layout.push_back({"Stream", true});
        return layout;
    }

    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override {
        lastLaneCount_ = laneCount;
        for (std::size_t ln = 0; ln < laneCount; ++ln)
            for (std::uint32_t f = 0; f < info.frames; ++f)
                outs[ln][f] += static_cast<float>(ln + 1);
    }

    std::size_t lastLaneCount() const { return lastLaneCount_; }

private:
    int         streams_      = 1;
    std::size_t lastLaneCount_ = 0;
};

// Run one FakeSystem through runUnit with the given router.
void driveOne(SystemBase* sys, const std::vector<std::unique_ptr<SystemBase>>& systems,
              const AudioRouter& router, std::uint32_t frames) {
    SystemBase* members[1] = {sys};
    AudioBlockInfo info{};
    info.frames     = frames;
    info.sampleRate = 48000.0;
    runUnit(info, members, 1, systems, router);
}

bool allEqual(const std::vector<float>& buf, float v) {
    for (float x : buf)
        if (x != v) return false;
    return true;
}

} // namespace

TEST_CASE("a 2-stream system is fanned into one stereo lane-pair per stream", "[audio][channels]") {
    std::vector<std::unique_ptr<SystemBase>> systems;
    systems.push_back(std::make_unique<FakeSystem>(1, /*streams=*/2));
    auto* fake = static_cast<FakeSystem*>(systems[0].get());

    const std::uint32_t frames = 8;
    std::vector<float> l0(frames, 0), r0(frames, 0), l1(frames, 0), r1(frames, 0);
    float* ls[2] = {l0.data(), l1.data()};
    float* rs[2] = {r0.data(), r1.data()};

    PerChannelRouter router(ls, rs, /*nStreams=*/2);
    driveOne(fake, systems, router, frames);

    CHECK(fake->lastLaneCount() == 4);  // 2 streams × stereo
    CHECK(allEqual(l0, 1.0f));          // stream 0 → outs[0]/outs[1]
    CHECK(allEqual(r0, 2.0f));
    CHECK(allEqual(l1, 3.0f));          // stream 1 → outs[2]/outs[3]
    CHECK(allEqual(r1, 4.0f));
}

TEST_CASE("a default single-stream system still finishes as plain stereo (2 lanes)", "[audio][channels]") {
    std::vector<std::unique_ptr<SystemBase>> systems;
    systems.push_back(std::make_unique<FakeSystem>(1, /*streams=*/1));
    auto* fake = static_cast<FakeSystem*>(systems[0].get());

    // channelLayout() default is a single stereo stream; a normal router reports streamCount == 1.
    CHECK(fake->channelLayout().size() == 1);

    const std::uint32_t frames = 8;
    std::vector<float> l(frames, 0), r(frames, 0);
    StereoRouter router(l.data(), r.data());
    CHECK(router.streamCount(0) == 1);

    driveOne(fake, systems, router, frames);

    CHECK(fake->lastLaneCount() == 2);  // the unchanged mix path
    CHECK(allEqual(l, 1.0f));
    CHECK(allEqual(r, 2.0f));
}
