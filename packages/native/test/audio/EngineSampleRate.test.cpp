// Guards that a host sample-rate change reaches ALREADY-LIVE cores. DPF's Plugin::sampleRateChanged (fired
// only while deactivated) calls Engine::setSampleRate, which must re-rate every adopted system in place —
// SameBoy's GB_set_sample_rate, Mesen's resampler — not just bake the new rate into cores built afterwards.
// Regression guard for the wiring that was missing: Engine::setSampleRate only updated its own scalar, so a
// loaded project kept rendering at its construct-time rate after the host switched (wrong pitch/tempo).
// Uses a fake system that records the rates it is handed, so the propagation is asserted directly.
//
// Run via `pnpm test:plugin`.

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "host/engine/Engine.hpp"
#include "system/SystemBase.hpp"

namespace {

// Records every rate it is activated / re-rated at, so the test can assert the Engine drove it.
class RateSpySystem final : public SystemBase {
public:
    explicit RateSpySystem(SystemId id) : SystemBase(id) {}
    SystemKind kind() const override { return SystemKind::SameBoy; }
    void onActivate(double sr) override { activatedAt = sr; }
    void onSampleRateChanged(double sr) override { rates.push_back(sr); }
    std::vector<ChannelStream> channelLayout() const override { return {{"Mix", true}}; }
    void finishBlock(const AudioBlockInfo&, float* const*, std::size_t) override {}

    double              activatedAt = 0.0;
    std::vector<double> rates; // every onSampleRateChanged rate, in order
};

} // namespace

TEST_CASE("Engine::setSampleRate re-rates already-live systems", "[audio][samplerate][engine]") {
    Engine eng(44100.0);

    auto sys = std::make_unique<RateSpySystem>(1);
    sys->onActivate(eng.sampleRate()); // built at the engine's current rate, as the factory does
    RateSpySystem* raw = sys.get();
    eng.adoptSystem(std::move(sys));

    REQUIRE(raw->activatedAt == 44100.0);
    REQUIRE(raw->rates.empty());

    // A host sample-rate change (via DPF sampleRateChanged) must reach the live core.
    eng.setSampleRate(48000.0);
    REQUIRE(raw->rates.size() == 1);
    CHECK(raw->rates.back() == 48000.0);
    CHECK(eng.sampleRate() == 48000.0);

    // A second change also propagates (not a one-shot).
    eng.setSampleRate(96000.0);
    REQUIRE(raw->rates.size() == 2);
    CHECK(raw->rates.back() == 96000.0);
}

TEST_CASE("Engine::setSampleRate on an empty project is a harmless no-op", "[audio][samplerate][engine]") {
    // The CLI + plugin set the rate before any system exists; that must not fault and must stick for the
    // cores built next.
    Engine eng(44100.0);
    eng.setSampleRate(48000.0);
    CHECK(eng.sampleRate() == 48000.0);
}
