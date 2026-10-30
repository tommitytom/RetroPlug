// Guards that a host sample-rate change reaches ALREADY-LIVE cores. DPF's Plugin::sampleRateChanged (fired
// only while deactivated) calls Engine::setSampleRate, which must re-rate every adopted system in place —
// SameBoy's GB_set_sample_rate, Mesen's resampler — not just bake the new rate into cores built afterwards.
// Regression guard for the wiring that was missing: Engine::setSampleRate only updated its own scalar, so a
// loaded project kept rendering at its construct-time rate after the host switched (wrong pitch/tempo).
// Drives a real Game Boy through the engine and reads the rate its core holds.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../engine-graph/GraphDocs.hpp"
#include "host/engine/Engine.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

TEST_CASE("Engine::setSampleRate re-rates already-live systems", "[audio][samplerate][engine]") {
    using namespace rp::host::test;
    Engine eng(44100.0);

    // A system node builds its core at the rate the plan runs at, as the factory does (the DMG boot ROM's chime needs
    // the model its settings name).
    std::vector<DocSystem> systems{{1, kGbType, 0, R"("model":"dmgB","fastBoot":false)"}};
    withRoms(eng, systems);
    REQUIRE(eng.setGraph(graphRequest(stereoDocument(systems), {1})).ok);
    const auto* raw = dynamic_cast<const SameBoySystem*>(eng.findSystem(1));
    REQUIRE(raw != nullptr);
    REQUIRE(raw->sampleRate_ == 44100.0);

    // A host sample-rate change (via DPF sampleRateChanged) must reach the live core.
    eng.setSampleRate(48000.0);
    CHECK(raw->sampleRate_ == 48000.0);
    CHECK(eng.sampleRate() == 48000.0);

    // A second change also propagates (not a one-shot), and the core still renders after it.
    eng.setSampleRate(96000.0);
    CHECK(raw->sampleRate_ == 96000.0);
    // About half a second at the new rate: the boot ROM's chime reaches the output.
    std::vector<float> l(512), r(512);
    float peak = 0.0f;
    for (int b = 0; b < 100; ++b) {
        eng.processBlock(512, l.data(), r.data());
        for (float x : l) peak = std::max(peak, std::abs(x));
    }
    CHECK(peak > 0.01f);
}

TEST_CASE("Engine::setSampleRate on an empty project is a harmless no-op", "[audio][samplerate][engine]") {
    // The CLI + plugin set the rate before any system exists; that must not fault and must stick for the
    // cores built next.
    Engine eng(44100.0);
    eng.setSampleRate(48000.0);
    CHECK(eng.sampleRate() == 48000.0);
}
