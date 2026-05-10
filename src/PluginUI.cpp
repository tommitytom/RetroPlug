/*
 * RetroPlug UI — thin shell.
 *
 * Responsibilities:
 *  - DPF lifecycle (constructor sizing, uiIdle, onResize, parameterChanged,
 *    uiFileBrowserSelected).
 *  - JS engine + bridge ownership.
 *  - Forward DPF keyboard events to LVGL focus AND to JS via the "key"
 *    event channel. Routing decisions live in TS.
 *  - Drain DSP→UI events (free released SystemBase pointers off the audio
 *    thread) and emit a per-tick "frame" event for React.
 *
 * This file deliberately knows nothing about menus, GB button mapping, or
 * framebuffer rendering. Those live in TS (ui/EmulatorTile.tsx,
 * runtime/lvgljs/input.ts, ui/PluginUI.tsx).
 */

#include "DistrhoUI.hpp"
#include "ResizeHandle.hpp"
#include "LvglJsEngine.hpp"
#include "PluginJsBridge.hpp"
#include "PluginShared.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

extern "C" {
    #include "lvgl.h"
}

#include "native/core/img/png/lodepng.h"

#include "transport/EventQueue.hpp"

extern "C" {
    extern const unsigned char ui_bundle_js[];
    extern const unsigned int  ui_bundle_js_len;
}

START_NAMESPACE_DISTRHO

// Fallback for plugin formats where DSP and UI live in separate binaries (LV2).
__attribute__((weak))
SharedDSPData* getSharedDSPData(void*) { return nullptr; }

// --------------------------------------------------------------------------------------------------------------------

class LVGLPluginUI : public UI
{
    float fGain = 0.0f;
    ResizeHandle fResizeHandle;
    LvglJsEngine jsEngine;
    std::unique_ptr<PluginJsBridge> bridge;
    SharedDSPData* shared = nullptr;

    // Screenshot hook (env-var triggered). When RETROPLUG_SCREENSHOT_PATH is
    // set, periodically dump the current LVGL screen as PNG. Cadence is
    // RETROPLUG_SCREENSHOT_INTERVAL_MS (default 1000). All work happens on
    // the UI thread inside uiIdle.
    std::string screenshotPath_;
    std::chrono::milliseconds screenshotInterval_{1000};
    std::chrono::steady_clock::time_point screenshotLast_{};

    void maybeWriteScreenshot()
    {
        if (screenshotPath_.empty()) return;

        const auto now = std::chrono::steady_clock::now();
        if (now - screenshotLast_ < screenshotInterval_) return;
        screenshotLast_ = now;

        lv_obj_t* screen = lv_screen_active();
        if (!screen) return;

        lv_draw_buf_t* snap = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);
        if (!snap) return;

        const uint32_t w = snap->header.w;
        const uint32_t h = snap->header.h;
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
        // ARGB8888 in memory is B,G,R,A on little-endian; transcode to RGB.
        const uint8_t* src = snap->data;
        for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
            rgb[i * 3 + 0] = src[i * 4 + 2];
            rgb[i * 3 + 1] = src[i * 4 + 1];
            rgb[i * 3 + 2] = src[i * 4 + 0];
        }

        const unsigned err = lodepng_encode24_file(screenshotPath_.c_str(),
                                                   rgb.data(), w, h);
        if (err)
            d_stderr("Screenshot lodepng error %u: %s",
                     err, lodepng_error_text(err));

        lv_draw_buf_destroy(snap);
    }

    // Drain SystemReleased events: the DSP shipped a displaced SystemBase
    // back so the UI thread can free it. Must run before anything else that
    // could observe a stale system pointer.
    void drainEvents()
    {
        if (!shared || !shared->events) return;
        Event ev;
        while (shared->events->tryPop(ev)) {
            switch (ev.kind) {
                case Event::Kind::SystemReleased:
                    delete ev.payload.systemReleased.system;
                    break;
                case Event::Kind::None:
                default:
                    break;
            }
        }
    }

public:
    LVGLPluginUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          fResizeHandle(this)
    {
        const double scaleFactor = getScaleFactor();

        if (d_isNotEqual(scaleFactor, 1.0))
        {
            setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH * scaleFactor, DISTRHO_UI_DEFAULT_HEIGHT * scaleFactor);
            setSize(DISTRHO_UI_DEFAULT_WIDTH * scaleFactor, DISTRHO_UI_DEFAULT_HEIGHT * scaleFactor);
        }
        else
        {
            setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);
        }

        if (isResizable())
            fResizeHandle.hide();

        if (const char* p = std::getenv("RETROPLUG_SCREENSHOT_PATH"); p && *p) {
            screenshotPath_ = p;
            if (const char* iv = std::getenv("RETROPLUG_SCREENSHOT_INTERVAL_MS"); iv && *iv) {
                if (int ms = std::atoi(iv); ms > 0)
                    screenshotInterval_ = std::chrono::milliseconds(ms);
            }
            d_stdout("Screenshot hook enabled: %s every %lld ms",
                     screenshotPath_.c_str(),
                     static_cast<long long>(screenshotInterval_.count()));
        }

        // In-process plugin formats: reach the DSP-owned Project via the shared
        // pointer. LV2-UI returns nullptr; the bridge degrades gracefully.
        if (void* dspPtr = getPluginInstancePointer())
            shared = getSharedDSPData(dspPtr);

        if (jsEngine.init())
        {
            jsEngine.setParamWriteCallback(
                [this](uint32_t idx, float val) {
                    editParameter(idx, true);
                    setParameterValue(idx, val);
                    editParameter(idx, false);
                });
            for (uint32_t i = 0; i < kPluginParameterCount; ++i)
                jsEngine.registerParameter(i, kPluginParameters[i].symbol);

            // Plugin-specific JS bridge. Must exist before evalModule so
            // useEffect handlers can register before the bundle's first render.
            bridge = std::make_unique<PluginJsBridge>(
                jsEngine,
                shared ? shared->project    : nullptr,
                shared ? shared->commands   : nullptr,
                shared ? shared->events     : nullptr,
                shared ? shared->sampleRate : nullptr);

            // The bridge calls this when JS invokes plugin.openRomBrowser.
            bridge->setOpenRomBrowserCallback([this]() {
                FileBrowserOptions opts;
                opts.title = "Open Game Boy ROM";
                openFileBrowser(opts);
            });

            const char* devPath = std::getenv("LVGL_PLUGIN_BUNDLE_PATH");
            if (devPath && *devPath)
            {
                if (jsEngine.evalModule(devPath) != 0)
                    d_stderr("Failed to load %s", devPath);
                else
                    d_stdout("LvglJsEngine: React UI loaded from %s", devPath);
            }
            else
            {
                if (jsEngine.evalModuleBuffer(reinterpret_cast<const char*>(ui_bundle_js),
                                              ui_bundle_js_len,
                                              "bundle.js") != 0)
                    d_stderr("Failed to load embedded UI bundle");
                else
                    d_stdout("LvglJsEngine: React UI loaded (embedded)");
            }
        }
        else
        {
            d_stderr("Failed to initialize LvglJsEngine");
        }
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == 0) fGain = value;
        jsEngine.pushParameter(index, value);
    }

    void uiIdle() override
    {
        drainEvents();

        // Emit a per-tick "frame" event so React's <EmulatorTile> knows to
        // poll plugin.getFrame. One emit per UI idle ties cadence to LVGL's
        // redraw — host-throttled when the window isn't visible.
        if (JSContext* ctx = jsEngine.getContext()) {
            jsEngine.emit("frame", 0, nullptr);
        }

        jsEngine.tick();
        maybeWriteScreenshot();
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return; // user cancelled
        if (!bridge) return;
        bridge->loadRomFromPath(filename);
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // Always forward to LVGL focus routing first — the parent chain
        // (LVGLWidget<TopLevelWidget>::onKeyboard) translates the DPF key
        // and writes it into LVGL's keyBuffer for the focused widget.
        UI::onKeyboard(ev);

        // Then mirror the event to JS so TS-side routing (game input,
        // menu toggle, etc.) can react. JS is the single source of truth
        // for keyboard policy.
        if (JSContext* ctx = jsEngine.getContext()) {
            JSValue args[2] = {
                JS_NewUint32(ctx, ev.key),
                JS_NewBool(ctx, ev.press),
            };
            jsEngine.emit("key", 2, args);
            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, args[1]);
        }
        return true;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LVGLPluginUI)
};

// --------------------------------------------------------------------------------------------------------------------

UI* createUI()
{
    return new LVGLPluginUI();
}

// --------------------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
