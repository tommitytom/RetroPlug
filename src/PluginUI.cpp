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
    // descriptor pointing at `frameBuffer_`. uiIdle() copies the latest
    // FrameBufferTriple snapshot into this buffer and invalidates the widget.
    // For Step 1 this is C++-owned; a React-side <EmulatorTile/> is a follow-up.
    lv_obj_t*               fbWidget    = nullptr;
    lv_image_dsc_t          fbDsc{};
    std::vector<std::uint8_t> fbStorage;
    SystemBase*             trackedSystem = nullptr;
    static constexpr int    kFbScale     = 2;

    // Cached default-system id so onKeyboard doesn't need to walk the project.
    // For Step 2 this is the only system; multi-instance refocus comes later.
    SystemId                trackedSystemId = 0;

    void ensureFramebufferWidget()
    {
        if (fbWidget) return;
        if (!shared || !shared->project) return;

        Project& proj = *shared->project;
        if (proj.systems().empty()) return;

        SystemBase* sys = proj.systems().front().get();
        if (!sys) return;
        FrameBufferTriple* fb = sys->framebuffer();
        if (!fb) return;

        const std::uint32_t w = fb->width();
        const std::uint32_t h = fb->height();
        fbStorage.assign(std::size_t(w) * h * 4, 0u);

        std::memset(&fbDsc, 0, sizeof(fbDsc));
        fbDsc.header.cf       = LV_COLOR_FORMAT_NATIVE; // XRGB8888 with LV_COLOR_DEPTH=32
        fbDsc.header.w        = w;
        fbDsc.header.h        = h;
        fbDsc.header.stride   = w * 4;
        fbDsc.data_size       = std::uint32_t(fbStorage.size());
        fbDsc.data            = fbStorage.data();

        fbWidget = lv_image_create(lv_screen_active());
        lv_image_set_src(fbWidget, &fbDsc);
        lv_image_set_scale(fbWidget, 256 * kFbScale); // 256 = 1.0x scale
        lv_obj_align(fbWidget, LV_ALIGN_TOP_MID, 0, 24);

        trackedSystem   = sys;
        trackedSystemId = sys->id();
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
            Project* proj = shared ? shared->project : nullptr;
            bridge = std::make_unique<PluginJsBridge>(jsEngine, proj);

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

            // Create the framebuffer widget after the React tree exists so the
            // image sits on top in z-order.
            ensureFramebufferWidget();
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
        // If the system was bootstrapped after the UI mounted (e.g. async ROM
        // load in the future), set up the widget now.
        if (!fbWidget) ensureFramebufferWidget();
        refreshFramebuffer();
        jsEngine.tick();
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // Translate to a Game Boy button and ship to the DSP. Unmapped keys
        // (Esc, Tab, etc.) return false so DPF/LVGL still routes them — the
        // React UI uses Esc to toggle the menu, for instance.
        GameboyButton button;
        if (!mapKeyToGameboyButton(ev.key, button))
            return false;
        if (shared && shared->commands) {
            shared->commands->tryPush(
                Command::makeButtonPress(trackedSystemId, button, ev.press));
        }
        return true; // event consumed
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
