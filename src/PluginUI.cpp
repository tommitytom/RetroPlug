/*
 * LVGL plugin example
 * Copyright (C) 2021 Jean Pierre Cimalando <jp-dev@inbox.ru>
 * Copyright (C) 2021-2022 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoUI.hpp"
#include "ResizeHandle.hpp"
#include "LvglJsEngine.hpp"
#include "PluginJsBridge.hpp"
#include "PluginShared.hpp"

#include <cstdlib>
#include <memory>

extern "C" {
    extern const unsigned char ui_bundle_js[];
    extern const unsigned int  ui_bundle_js_len;
}

START_NAMESPACE_DISTRHO

// Fallback for plugin formats where DSP and UI are in separate binaries (e.g. LV2).
// The real implementation lives in PluginDSP.cpp. When linked together (VST, CLAP, etc.),
// the strong definition wins. For LV2-UI (separate .so), this weak definition provides
// a safe fallback that returns nullptr.
__attribute__((weak))
SharedDSPData* getSharedDSPData(void*) { return nullptr; }

// --------------------------------------------------------------------------------------------------------------------

class LVGLPluginUI : public UI
{
    float fGain = 0.0f;
    float fFreq = 440.0f;
    int fShape = 0;
    ResizeHandle fResizeHandle;
    LvglJsEngine jsEngine;
    std::unique_ptr<PluginJsBridge> bridge;
    SharedDSPData* shared = nullptr;
    float waveformBuf[256];
    uint32_t waveformPoints = 0;

    // ----------------------------------------------------------------------------------------------------------------

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

        // Get direct access to the DSP instance's shared data (in-process formats only)
        void* dspPtr = getPluginInstancePointer();
        if (dspPtr)
            shared = getSharedDSPData(dspPtr);

        if (jsEngine.init())
        {
            // Generic parameter machinery is owned by the engine. Wire up the
            // host-write callback and register parameter names so JS can
            // address them by name (and typos throw at the JS layer).
            jsEngine.setParamWriteCallback(
                [this](uint32_t idx, float val) {
                    editParameter(idx, true);
                    setParameterValue(idx, val);
                    editParameter(idx, false);
                });
            for (uint32_t i = 0; i < kPluginParameterCount; ++i)
                jsEngine.registerParameter(i, kPluginParameters[i].symbol);

            // Plugin-specific JS bridge: place to add custom DSP↔UI bridges
            // (e.g. waveform). Must exist before evalModule so React's
            // useEffect hooks can register handlers via lvgljs.on() at module
            // load.
            bridge = std::make_unique<PluginJsBridge>(jsEngine);

            // Dev override: load from a file on disk if LVGL_PLUGIN_BUNDLE_PATH is set.
            // Otherwise use the bundle embedded into this binary at build time.
            // Note: some DAWs sanitize the env, especially on macOS — the override is
            // intended for jalv/Carla/Reaper-style dev workflows.
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
        switch (index)
        {
        case 0: fGain  = value; break;
        case 1: fFreq  = value; break;
        case 2: fShape = (value > 0.5f) ? 1 : 0; break;
        default: return;
        }
        jsEngine.pushParameter(index, value);
        repaint();
    }

    void uiIdle() override
    {
        // Drain waveform data from DSP ring buffer
        if (shared != nullptr)
        {
            HeapRingBuffer& ring = shared->waveformRing;

            // Read latest snapshot, skip older ones
            while (ring.isDataAvailableForReading())
            {
                const uint32_t count = ring.readUInt();
                if (count == 0 || count > 256)
                    break;
                ring.readCustomData(waveformBuf, count * sizeof(float));
                waveformPoints = count;
            }

            if (waveformPoints > 0 && bridge)
                bridge->pushWaveform(waveformBuf, waveformPoints);
        }

        jsEngine.tick();
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
