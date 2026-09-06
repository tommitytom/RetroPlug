#pragma once

// In-process handoff from the DSP plugin to its DPF editor.
//
// The plugin owns a plugin-lifetime TjsHostRuntime with __rpcSend already bound to the backend services
// (bootControlPlane). The editor reaches it via getPluginInstancePointer() → getSharedDSP(),
// then attaches its LVGL display layer to that host (LvglJsEngine::useExternalHost) so the React UI runs
// on the SAME context as the control plane — the backend is reachable through the existing
// Symbol.for("plugin").__rpcSend, no separate RPC bridge needed.
//
// The plugin ships no separate-binary UI format (clap/vst3/jack all link DSP+UI into one binary), so
// there is no LV2-style null fallback to handle. Include this AFTER DistrhoPlugin.hpp / DistrhoUI.hpp so
// START_NAMESPACE_DISTRHO is defined.

#include <functional>

class TjsHostRuntime;

START_NAMESPACE_DISTRHO

struct SharedDSP {
    TjsHostRuntime* host = nullptr; // the plugin's control-plane host (useExternalHost target)
    // Re-read the per-ROM DAW parameter map and re-label the plugin's CC slots if it moved
    // (spec/12-dynamic-parameters.md). The editor drives this from uiIdle because a ROM loaded through
    // the UI changes what the parameters mean without ever passing through DPF's setState. Cheap when
    // nothing changed (one JS call + a string compare), and main-thread only — which uiIdle is.
    std::function<void()> pollParameterMap;
};

// Defined in PluginDSP.cpp — returns the plugin instance's shared struct.
SharedDSP* getSharedDSP(void* pluginPtr);

END_NAMESPACE_DISTRHO
