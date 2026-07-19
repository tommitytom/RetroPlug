#pragma once

// Backend-agnostic headless render scaffold.
//
// The reusable "render half" of a headless UI test: boot a React UI bundle on top of LvglJsEngine with
// no DPF / PUGL / OpenGL / Xvfb. It replicates only the cheap, non-GL subset of DPF's LVGLWidget
// (deps/dpf-widgets/generic/LVGL.cpp): an lv_display with a CPU draw buffer + a no-op flush, the default
// group + input devices, then lv_timer_handler driven manually. The widget tree → pixels path is pure
// software, so lv_snapshot_take gives the rendered frame and the lv_binding_js comp_map lets us assert
// on the rendered widgets.
//
// This class knows NOTHING about any backend — no Project, no RPC, no command queue. A harness composes
// it with a backend (which binds `__rpcSend` into `engine().host()` and evals a UI bundle). One
// RenderCore owns one lv_display + one JS runtime; construct one per test case.
//
// (A fresh mirror of the render half of packages/native/test/ui/UiTestHarness.cpp; the legacy harness
// stays as-is and is deleted with legacy.)

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
    #include "lvgl.h"
}

#include "dpfjs/LvglJsEngine.hpp"
#include "host/ui/SoftwareLvglDisplay.hpp" // shared LvInputState + display/indev scaffold (also used by sdl/)

namespace rpuigf {

// A rendered ARGB8888 snapshot (B,G,R,A in memory, little-endian).
struct Snapshot {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> argb; // width*height*4

    bool isFlat() const;            // every pixel identical (nothing rendered)
};

// A located widget's geometry + content, for TS assertions. Coordinates are ABSOLUTE (screen-relative,
// via lv_obj_get_coords) so clickAt can target them.
struct WidgetInfo {
    bool          found  = false;
    std::int32_t  x = 0, y = 0, width = 0, height = 0;
    std::uint32_t childCount = 0;
    std::uint32_t state = 0; // lv_obj_get_state bitmask (LV_STATE_FOCUSED/HOVERED/…) — for hover assertions
    std::string   text;  // non-empty only for COMP_TYPE_TEXT (lv_label) widgets
};

class RenderCore {
public:
    explicit RenderCore(std::uint32_t width = 480, std::uint32_t height = 432);
    ~RenderCore();

    RenderCore(const RenderCore&) = delete;
    RenderCore& operator=(const RenderCore&) = delete;

    // Build the LVGL display + JS engine and install the testId hook. Returns false if any stage
    // fails. Call once, BEFORE binding a backend / evaling a UI bundle. Does NOT eval a bundle or
    // attach the display — a harness does that after binding its RPC surface into engine().host().
    bool init();

    // The JS engine — a harness reaches engine().host() to bind __rpcSend, evalModuleBytecode to load
    // its UI bundle, and attachDisplay to mount React.
    LvglJsEngine& engine() { return engine_; }

    // Advance the render loop `iterations` times: emit "frame", run the JS event loop + LVGL
    // layout/redraw, ~23ms of simulated tick each. NO backend step (no command drain, no emulator
    // advance) — a harness layers those on top if it needs them. The initial React render needs
    // several iterations for its RPC round-trips to settle.
    void pump(int iterations = 30);

    // Detach + re-attach the display on the SAME runtime (unmount → flush async deletes → re-mount).
    // Proves the QuickJS context survives a reopen without re-eval.
    void reopenEditor();

    // -- assertions surface -------------------------------------------------
    Snapshot snapshot();                        // render the active screen to ARGB
    bool snapshotPng(const std::string& path);  // also write a PNG (RGB)

    std::size_t widgetCount() const;            // total live lv_binding_js components
    std::size_t countByType(int compType) const;// ECOMP_TYPE
    lv_obj_t* findFirstByType(int compType) const;
    lv_obj_t* findByText(const std::string& text) const;            // first exact label match
    lv_obj_t* findByTextContaining(const std::string& substr) const; // first label containing substr
    lv_obj_t* findByTestId(const std::string& name) const;          // via the __rp_tagTestId hook

    WidgetInfo widgetInfo(lv_obj_t* obj) const; // geometry + text (absolute coords)
    lv_obj_t* focusedObject() const;            // the keypad group's focused widget
    lv_obj_t* screen() const { return lv_screen_active(); }

    // -- runner plumbing (the test bundle runs in the engine's own JS runtime) --------------------
    JSContext* engineContext() { return engine_.getContext(); }
    int evalTestModule(const char* code, std::size_t len, const char* name) {
        return engine_.evalModuleBuffer(code, len, name);
    }
    void drainJs() { engine_.tick(); } // pump the JS event loop (no render step)

    // -- input driving ------------------------------------------------------
    void tapKey(std::uint32_t lvKey);           // LV_KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC
    void clickAt(std::int32_t x, std::int32_t y);// press+release at absolute (x,y)
    void moveMouse(std::int32_t x, std::int32_t y);// move the (unpressed) pointer → LVGL hover at (x,y)
    // Synthesize the native SDL-poll buses so a test can drive gamepad input (menu nav / game routing).
    // Arg shapes mirror PluginUI::pumpGamepad exactly: button [pad, name, press]; axis [pad, name, value].
    void gamepadButton(int pad, const std::string& name, bool press);
    void gamepadAxis(int pad, const std::string& name, double value);
    // Synthesize the native file-drop bus (PluginUI::uiFileDropped): newline-joined paths + window-space
    // drop coords. The App's useNativeEvent("file-drop") handler routes it (load / replace / load-sram).
    void fileDropped(const std::string& pathsNewlineJoined, std::int32_t x, std::int32_t y);

    // Called by the __rp_tagTestId JS trampoline (UI ref hook).
    void recordTestId(const std::string& name, lv_obj_t* obj) { testIds_[name] = obj; }

private:
    void installTestIdHook();  // adds globalThis.__rp_tagTestId

    std::uint32_t width_;
    std::uint32_t height_;

    lv_display_t* display_ = nullptr;
    std::vector<std::uint8_t> drawBuf_;
    lv_group_t* group_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    lv_indev_t* pointer_ = nullptr;

    // Synthetic input state, read by the shared keypad/pointer indev callbacks (driver_data == &input_).
    retroplug::ui::LvInputState input_;

    LvglJsEngine engine_;

    std::unordered_map<std::string, lv_obj_t*> testIds_;
    bool booted_ = false;
};

} // namespace rpuigf
