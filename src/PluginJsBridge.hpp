#pragma once

#include <cstdint>

#include "LvglJsEngine.hpp"

// Plugin-specific glue between LvglJsEngine and DPF. Generic parameter handling
// (lvgljs.setParameter, name<->index lookup, "parameter" event push) lives in
// LvglJsEngine itself; this class is the place to add JS bridges that only
// make sense for *this* plugin — currently just the waveform display.
//
// Lifetime: must be destroyed before the LvglJsEngine it references.
class PluginJsBridge {
public:
    explicit PluginJsBridge(LvglJsEngine& engine);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&) = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    void pushWaveform(const float* samples, uint32_t count);

private:
    LvglJsEngine& engine;
};
