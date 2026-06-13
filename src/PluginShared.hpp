#pragma once

#include <atomic>

#include "dpfjs/PluginDescriptor.hpp"
#include "project/Project.hpp"
#include "system/SystemTypes.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"

// Both PluginDSP.cpp (via DistrhoPlugin.hpp) and PluginUI.cpp (via DistrhoUI.hpp)
// pull DistrhoDetails.hpp before this header, which is what defines the
// kParameterIs* hint flags used in the table below.

START_NAMESPACE_DISTRHO

// Direct-access channel between DSP and UI for in-process plugin formats
// (VST2/3, CLAP, AU, JACK). The UI calls getSharedDSPData() with its
// getPluginInstancePointer() to reach the DSP-owned project + transports.
// LV2 (separate-binary UI) returns nullptr; UI degrades gracefully.
struct SharedDSPData {
    Project*             project    = nullptr;
    CommandQueue*        commands   = nullptr;
    EventQueue*          events     = nullptr;
    // DSP writes on activate / sampleRateChanged. UI reads when constructing
    // a SameBoySystem off the audio thread so onActivate can configure
    // GB_set_sample_rate at the right rate. Atomic because UI may read
    // concurrently with DSP-side updates.
    std::atomic<double>* sampleRate = nullptr;
    // Multi-instance focus: the system id that currently owns keyboard input.
    // 0 = no focus / empty project. Written by the UI thread (Tab cycling,
    // tile click) and read by the UI thread for chrome plus by the bridge
    // when routing pressButton commands without an explicit systemId arg.
    std::atomic<SystemId>* focusedSystemId = nullptr;
};

// Implemented in PluginDSP.cpp — returns the SharedDSPData from the plugin instance.
SharedDSPData* getSharedDSPData(void* pluginPtr);

// RetroPlug's plugin descriptor: the runtime parameter list + identity the
// dpf.js framework consumes (PluginDSP's initParameter copies the spec fields
// onto DPF's Parameter struct; PluginUI registers symbol -> index with the JS
// engine). The array order defines parameter indices (0, 1, 2, ...). The
// identity here is the runtime truth; DistrhoPluginInfo.h is the compile-time
// DPF identity and must agree (name/URI/IO).
constexpr dpfjs::ParamSpec kRetroPlugParams[] = {
    { "gain", "Master Gain", "Gain", "dB", -90.0f, 12.0f, 0.0f,
      kParameterIsAutomatable },
};

constexpr dpfjs::PluginDescriptor kRetroPlugDescriptor{
    "RetroPlug", "urn:distrho:retroplug", 0, 8, kRetroPlugParams,
};

END_NAMESPACE_DISTRHO
