#pragma once

#include "project/Project.hpp"

// Both PluginDSP.cpp (via DistrhoPlugin.hpp) and PluginUI.cpp (via DistrhoUI.hpp)
// pull DistrhoDetails.hpp before this header, which is what defines the
// kParameterIs* hint flags used in the table below.

START_NAMESPACE_DISTRHO

// Direct-access channel between DSP and UI for in-process plugin formats
// (VST2/3, CLAP, AU, JACK). The UI calls getSharedDSPData() with its
// getPluginInstancePointer() to reach the DSP-owned Project — and from there,
// each SystemBase's FrameBufferTriple. LV2 has no shared pointer, so the UI
// gets nullptr and renders a placeholder.
struct SharedDSPData {
    Project* project = nullptr;
};

// Implemented in PluginDSP.cpp — returns the SharedDSPData from the plugin instance.
SharedDSPData* getSharedDSPData(void* pluginPtr);

// Single source of truth for the plugin's parameters. Consumed by both
// PluginDSP.cpp (initParameter copies fields onto DPF's Parameter struct)
// and PluginUI.cpp (registers symbol -> index with the JS engine).
// The order here defines parameter indices (0, 1, 2, ...).
struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* shortName;
    const char* unit;
    float min;
    float max;
    float def;
    uint32_t hints;
};

constexpr ParamSpec kPluginParameters[] = {
    { "gain", "Master Gain", "Gain", "dB", -90.0f, 12.0f, 0.0f,
      kParameterIsAutomatable },
};

constexpr uint32_t kPluginParameterCount =
    sizeof(kPluginParameters) / sizeof(kPluginParameters[0]);

END_NAMESPACE_DISTRHO
