#pragma once

#include "extra/RingBuffer.hpp"

// Both PluginDSP.cpp (via DistrhoPlugin.hpp) and PluginUI.cpp (via DistrhoUI.hpp)
// pull DistrhoDetails.hpp before this header, which is what defines the
// kParameterIs* hint flags used in the table below.

START_NAMESPACE_DISTRHO

// Shared data between DSP and UI for direct-access data transfer.
// Owned by the DSP plugin, accessed by the UI via getPluginInstancePointer().
// Only works for in-process plugin formats (VST2/3, CLAP, AU, JACK).
struct SharedDSPData {
    static constexpr uint32_t kWaveformPoints = 256;
    HeapRingBuffer waveformRing;

    SharedDSPData() {
        waveformRing.createBuffer(kWaveformPoints * sizeof(float) * 8);
    }
};

// Implemented in PluginDSP.cpp — returns the SharedDSPData from the plugin instance.
// Only valid for in-process plugin formats where DSP and UI share a binary.
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
    { "gain",  "Gain",      "Gain",  "dB", -90.0f, 30.0f,    -50.0f,
      kParameterIsAutomatable },
    { "freq",  "Frequency", "Freq",  "Hz",  20.0f, 20000.0f, 440.0f,
      kParameterIsAutomatable | kParameterIsLogarithmic },
    { "shape", "Shape",     "Shape", "",    0.0f,  1.0f,     0.0f,
      kParameterIsAutomatable | kParameterIsInteger | kParameterIsBoolean },
};

constexpr uint32_t kPluginParameterCount =
    sizeof(kPluginParameters) / sizeof(kPluginParameters[0]);

END_NAMESPACE_DISTRHO
