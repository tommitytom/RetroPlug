#pragma once

// Headless UI test harness.
//
// Boots the *real* React UI bundle on top of LvglJsEngine + PluginJsBridge with
// no DPF / PUGL / OpenGL / Xvfb. It replicates only the cheap, non-GL subset of
// DPF's LVGLWidget (deps/dpf-widgets/generic/LVGL.cpp): create an lv_display with
// a CPU draw buffer + a no-op flush, the default group + input devices, then drive
// lv_timer_handler manually. The widget tree → pixels path is pure software, so
// lv_snapshot_take gives us the rendered frame, and the global lv_binding_js
// comp_map lets us assert on the rendered widgets.
//
// One harness owns one lv_display + one JS runtime; construct one per test case.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
    #include "lvgl.h"
}

#include "LvglJsEngine.hpp"
#include "project/Project.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"

class PluginJsBridge;

namespace rpui {

// A rendered ARGB8888 snapshot (B,G,R,A in memory, little-endian).
struct Snapshot {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> argb; // width*height*4

    bool isFlat() const;            // every pixel identical (nothing rendered)
};

class UiTestHarness {
public:
    explicit UiTestHarness(std::uint32_t width = 480, std::uint32_t height = 432);
    ~UiTestHarness();

    UiTestHarness(const UiTestHarness&) = delete;
    UiTestHarness& operator=(const UiTestHarness&) = delete;

    // Build the LVGL display + JS engine + bridge and boot the embedded UI
    // bundle. Returns false if any stage fails. Call once.
    bool boot();

    // Load a ROM into the project (same path as cli/TestHarness.cpp) and tell the
    // UI to re-query. Call after boot(); then pump() so the tile mounts/renders.
    std::uint32_t loadRom(const std::string& path);

    // Advance the UI: pump the in-process RPC, emit "frame", run the JS loop and
    // LVGL layout/redraw, `iterations` times (each ~16ms of simulated tick). The
    // initial React render needs several iterations for its RPC round-trips to
    // settle before the tree is stable.
    void pump(int iterations = 30);

    // Tell the UI to refetch systems (emits "config-changed"); use after loadRom.
    void notifyConfigChanged();

    // -- assertions surface -------------------------------------------------

    Snapshot snapshot();                 // render the active screen to ARGB
    bool snapshotPng(const std::string& path); // also write a PNG (RGB)

    std::size_t widgetCount() const;     // total live lv_binding_js components
    std::size_t countByType(int compType) const;   // ECOMP_TYPE
    lv_obj_t* findFirstByType(int compType) const;
    lv_obj_t* findByText(const std::string& text) const; // first COMP_TYPE_TEXT match
    lv_obj_t* findByTestId(const std::string& name) const; // via the __rp_tagTestId hook

    lv_obj_t* screen() const { return lv_screen_active(); }

    // Called by the __rp_tagTestId JS trampoline (UI ref hook).
    void recordTestId(const std::string& name, lv_obj_t* obj) { testIds_[name] = obj; }

private:
    void installTestIdHook();            // adds globalThis.__rp_tagTestId

    std::uint32_t width_;
    std::uint32_t height_;

    lv_display_t* display_ = nullptr;
    std::vector<std::uint8_t> drawBuf_;
    lv_group_t* group_ = nullptr;
    lv_indev_t* keypad_ = nullptr;

    Project project_;
    CommandQueue commands_;
    EventQueue events_;
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<SystemId> focusedSystemId_{0};

    // Scratch audio buffers: pump() runs project_.onProcess each iteration so
    // the emulator advances and publishes framebuffers (there is no DSP thread
    // headless). The mixed audio is discarded.
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;

    LvglJsEngine engine_;
    std::unique_ptr<PluginJsBridge> bridge_; // after engine_: destructs first

    std::unordered_map<std::string, lv_obj_t*> testIds_;
    bool booted_ = false;
};

} // namespace rpui
