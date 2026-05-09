/*
 * RetroPlug UI — Step 1: SameBoy framebuffer + master gain.
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoUI.hpp"
#include "ResizeHandle.hpp"
#include "LvglJsEngine.hpp"
#include "PluginJsBridge.hpp"
#include "PluginShared.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
    #include "lvgl.h"
}

#include "project/Project.hpp"
#include "system/InputTypes.hpp"
#include "system/SystemBase.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/FrameBufferTriple.hpp"

extern "C" {
    extern const unsigned char ui_bundle_js[];
    extern const unsigned int  ui_bundle_js_len;
}

START_NAMESPACE_DISTRHO

// Fallback for plugin formats where DSP and UI live in separate binaries (LV2).
// The strong definition is in PluginDSP.cpp; for the LV2-UI .so this weak one
// keeps the link working and just returns nullptr (UI shows a placeholder).
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

    // Direct-draw framebuffer surface. lv_image widget with a persistent
    // descriptor pointing at `fbStorage`. uiIdle() copies the latest
    // FrameBufferTriple snapshot into this buffer and invalidates the widget.
    // For Step 1 this is C++-owned; a React-side <EmulatorTile/> is a follow-up.
    //
    // Sizing/scaling: the widget is sized to the full LVGL screen and uses
    // LV_IMAGE_ALIGN_CONTAIN to scale the framebuffer to the largest size
    // that fits while preserving aspect ratio. Antialiasing disabled for
    // crisp nearest-neighbor pixels.
    lv_obj_t*               fbWidget    = nullptr;
    lv_image_dsc_t          fbDsc{};
    std::vector<std::uint8_t> fbStorage;
    SystemBase*             trackedSystem = nullptr;

    // Cached default-system id so onKeyboard doesn't need to walk the project.
    // For Step 2 this is the only system; multi-instance refocus comes later.
    SystemId                trackedSystemId = 0;

    // True while React owns keyboard input (e.g. menu open). onKeyboard then
    // returns false for non-Esc keys so LVGL routes them to the focused
    // widget (arrow nav, Enter to select). Toggled by JS via
    // plugin.setUiCapturesKeyboard.
    bool                    uiCapturesKeyboard_ = false;

    void resizeFramebufferWidgetToScreen()
    {
        if (!fbWidget) return;
        lv_obj_t* screen = lv_screen_active();
        if (!screen) return;
        const lv_coord_t w = lv_obj_get_width(screen);
        const lv_coord_t h = lv_obj_get_height(screen);
        lv_obj_set_size(fbWidget, w, h);
        lv_obj_set_pos(fbWidget, 0, 0);
        lv_obj_invalidate(fbWidget);
    }

    // Create the framebuffer widget once, before the React tree loads, so it
    // sits behind React in z-order. Hidden until a system is loaded.
    void createFramebufferWidget()
    {
        if (fbWidget) return;

        fbWidget = lv_image_create(lv_screen_active());
        lv_image_set_antialias(fbWidget, false);
        lv_image_set_inner_align(fbWidget, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_remove_flag(fbWidget, LV_OBJ_FLAG_SCROLLABLE);
        lv_image_set_pivot(fbWidget, 0, 0);
        lv_obj_add_flag(fbWidget, LV_OBJ_FLAG_HIDDEN);

        resizeFramebufferWidgetToScreen();
    }

    // Re-point the lv_image at `sys`'s framebuffer. Allocates fbStorage to
    // match (only if dimensions change). Hides the widget if `sys` is null.
    void retargetFramebufferWidget(SystemBase* sys)
    {
        if (!fbWidget) return;
        if (!sys) {
            lv_image_set_src(fbWidget, nullptr);
            lv_obj_add_flag(fbWidget, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        FrameBufferTriple* fb = sys->framebuffer();
        if (!fb) {
            lv_obj_add_flag(fbWidget, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        const std::uint32_t w = fb->width();
        const std::uint32_t h = fb->height();
        const std::size_t   bytes = std::size_t(w) * h * 4;
        if (fbStorage.size() != bytes)
            fbStorage.assign(bytes, 0u);

        std::memset(&fbDsc, 0, sizeof(fbDsc));
        fbDsc.header.cf     = LV_COLOR_FORMAT_NATIVE;
        fbDsc.header.w      = w;
        fbDsc.header.h      = h;
        fbDsc.header.stride = w * 4;
        fbDsc.data_size     = std::uint32_t(bytes);
        fbDsc.data          = fbStorage.data();

        lv_image_cache_drop(&fbDsc);
        lv_image_set_src(fbWidget, &fbDsc);
        lv_obj_remove_flag(fbWidget, LV_OBJ_FLAG_HIDDEN);
        resizeFramebufferWidgetToScreen();
    }

    // Drain SystemReleased events: the DSP shipped a displaced SystemBase
    // back so the UI thread can free it. Must run BEFORE we read trackedSystem,
    // since the DSP may have just swapped trackedSystem out from under us.
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

    // Re-resolve trackedSystem from the project. Called every uiIdle to pick
    // up ROM-swap changes. Always runs after drainEvents so any dangling
    // SystemBase pointers have already been freed.
    void resolveTrackedSystem()
    {
        SystemBase* current = nullptr;
        if (shared && shared->project && !shared->project->systems().empty())
            current = shared->project->systems().front().get();
        if (current == trackedSystem) return;
        trackedSystem   = current;
        trackedSystemId = current ? current->id() : 0;
        retargetFramebufferWidget(current);
    }

    // Map a DPF KeyboardEvent's `key` (Unicode point or kKey* sentinel) to a
    // GameboyButton. Returns false for keys we don't bind (caller forwards
    // them to the React UI for menu navigation, etc.). The mapping mirrors a
    // typical GB emulator front-end.
    static bool mapKeyToGameboyButton(uint key, GameboyButton& out)
    {
        switch (key) {
            case kKeyLeft:      out = GameboyButton::Left;   return true;
            case kKeyRight:     out = GameboyButton::Right;  return true;
            case kKeyUp:        out = GameboyButton::Up;     return true;
            case kKeyDown:      out = GameboyButton::Down;   return true;
            case kKeyEnter:     out = GameboyButton::Start;  return true;
            case kKeyShiftR:
            case kKeyShiftL:
            case kKeyBackspace: out = GameboyButton::Select; return true;
            case 'z': case 'Z': out = GameboyButton::A;      return true;
            case 'x': case 'X': out = GameboyButton::B;      return true;
            default:                                          return false;
        }
    }

    void refreshFramebuffer()
    {
        if (!fbWidget || !trackedSystem) return;
        FrameBufferTriple* fb = trackedSystem->framebuffer();
        if (!fb) return;
        const std::uint32_t pixels = fb->width() * fb->height();
        if (fb->readInto(reinterpret_cast<std::uint32_t*>(fbStorage.data()), pixels)) {
            lv_image_cache_drop(&fbDsc);
            lv_obj_invalidate(fbWidget);
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

        // In-process plugin formats: reach the DSP-owned Project via the shared
        // pointer. LV2-UI returns nullptr; the framebuffer simply doesn't render
        // (audio still works via the DSP binary).
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

            // React raises this on menu mount / lowers on unmount.
            bridge->setUiCapturesKeyboardCallback([this](bool captured) {
                uiCapturesKeyboard_ = captured;
            });

            // Create the framebuffer widget BEFORE the React bundle loads so
            // the React widget tree z-orders above the framebuffer (LVGL
            // draws siblings in creation order). React uses a transparent
            // root so the framebuffer shows through; menu overlays still
            // appear on top. Widget is hidden until a system is loaded.
            createFramebufferWidget();

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
        repaint();
    }

    void uiIdle() override
    {
        // 1. Drain DSP→UI events first. SystemReleased events delete displaced
        //    systems on the UI thread (DSP must never free).
        drainEvents();
        // 2. Re-resolve trackedSystem after drainEvents — the DSP may have
        //    swapped a system in or out since last tick.
        resolveTrackedSystem();
        // 3. Pull the latest framebuffer snapshot if we have a system.
        if (trackedSystem) refreshFramebuffer();
        jsEngine.tick();
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return; // user cancelled
        if (!bridge) return;
        bridge->loadRomFromPath(filename);
    }

    void onResize(const ResizeEvent& ev) override
    {
        // DPF's UI::onResize chain forwards to LVGLWidget<TopLevelWidget>::onResize
        // which updates lv_display's resolution. Run that first; then re-fit
        // our framebuffer widget to the new screen size.
        UI::onResize(ev);
        resizeFramebufferWidgetToScreen();
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // Note: this method REPLACES LVGLWidget<TopLevelWidget>::onKeyboard
        // (which is what writes keys into LVGL's keyBuffer). To let LVGL
        // route a key to the focused widget, we must explicitly chain to
        // UI::onKeyboard(ev). Returning false from here without chaining
        // just drops the key.

        // Esc toggles the React menu. Bypass focus routing because the
        // React tree has nothing focusable while the menu is closed.
        if (ev.key == kKeyEscape) {
            if (ev.press)
                jsEngine.emit("esc-pressed", 0, nullptr);
            return true; // consume — don't forward to the GB or LVGL
        }

        // While React owns keyboard input (menu open), forward to LVGL so
        // arrow nav / Enter / Tab reach the focused React widget.
        if (uiCapturesKeyboard_)
            return UI::onKeyboard(ev);

        // Translate to a Game Boy button and ship to the DSP. Unmapped keys
        // chain to LVGL so anything we don't claim still drives the UI.
        GameboyButton button;
        if (!mapKeyToGameboyButton(ev.key, button))
            return UI::onKeyboard(ev);
        if (shared && shared->commands) {
            shared->commands->tryPush(
                Command::makeButtonPress(trackedSystemId, button, ev.press));
        }
        return true; // event consumed by GB
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
