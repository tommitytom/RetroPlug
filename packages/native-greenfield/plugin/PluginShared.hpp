#pragma once

// In-process handoff from the greenfield DSP plugin to its DPF editor.
//
// The plugin owns a plugin-lifetime TjsHostRuntime with __rpcSend already bound to the BackendFacade
// (bootControlPlane). The editor reaches it via getPluginInstancePointer() → getSharedDSP(),
// then attaches its LVGL display layer to that host (LvglJsEngine::useExternalHost) so the React UI runs
// on the SAME context as the control plane — the backend is reachable through the existing
// Symbol.for("plugin").__rpcSend, no separate RPC bridge needed.
//
// Greenfield ships no separate-binary UI format (clap/vst3/jack all link DSP+UI into one binary), so
// there is no LV2-style null fallback to handle. Include this AFTER DistrhoPlugin.hpp / DistrhoUI.hpp so
// START_NAMESPACE_DISTRHO is defined.

class TjsHostRuntime;

START_NAMESPACE_DISTRHO

struct SharedDSP {
    TjsHostRuntime* host = nullptr; // the plugin's control-plane host (useExternalHost target)
};

// Defined in PluginDSP.cpp — returns the plugin instance's shared struct.
SharedDSP* getSharedDSP(void* pluginPtr);

END_NAMESPACE_DISTRHO
