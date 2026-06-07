// Headless UI smoke tests — the proof-of-concept.
//
// Boots the real React UI bundle with no DPF / window / OpenGL / Xvfb, renders
// it to a software buffer, and asserts on the rendered LVGL tree + snapshot.
// Runs synchronously in well under a second per case.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <string>

#include "UiTestHarness.hpp"

extern "C" {
    #include "native/core/basic/comp.hpp"  // ECOMP_TYPE (COMP_TYPE_TEXT / _IMAGE)
}

#ifndef RP_ROM_DIR
#define RP_ROM_DIR "resources/roms"
#endif

using clk = std::chrono::steady_clock;
static long ms_since(clk::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count();
}

TEST_CASE("headless UI boots and renders start-screen chrome", "[ui]") {
    const auto t0 = clk::now();

    rpui::UiTestHarness h;
    REQUIRE(h.boot());
    h.pump(40); // let the initial RPC round-trips + React render settle

    const rpui::Snapshot snap = h.snapshot();
    CHECK(snap.width == 480);
    CHECK(snap.height == 432);
    CHECK_FALSE(snap.isFlat());                 // something actually rendered
    CHECK(h.widgetCount() > 0);                 // live lv_binding_js components
    CHECK(h.countByType(COMP_TYPE_TEXT) > 0);   // StartScreen menu labels

    CHECK(h.snapshotPng("/tmp/ui-test-start.png"));
    std::printf("[ui-test] boot+render+snapshot (empty project): %ld ms\n", ms_since(t0));
}

TEST_CASE("loading a ROM mounts an emulator tile", "[ui]") {
    const auto t0 = clk::now();

    rpui::UiTestHarness h;
    REQUIRE(h.boot());
    h.pump(40);
    CHECK(h.findByTestId("slot-0") == nullptr); // no tiles before any ROM

    const std::uint32_t id = h.loadRom(std::string(RP_ROM_DIR) + "/mGB.gb");
    h.pump(60); // mount the SystemGrid + tile, poll a few frames

    // The per-system slot wrapper was tagged via the UI ref -> __rp_tagTestId.
    lv_obj_t* slot = h.findByTestId("slot-" + std::to_string(id));
    CHECK(slot != nullptr);
    // The EmulatorTile renders its framebuffer through a <Canvas> (lv_image).
    CHECK(h.countByType(COMP_TYPE_IMAGE) > 0);
    CHECK_FALSE(h.snapshot().isFlat());

    CHECK(h.snapshotPng("/tmp/ui-test-tile.png"));
    std::printf("[ui-test] boot+load mGB+render: %ld ms\n", ms_since(t0));
}
