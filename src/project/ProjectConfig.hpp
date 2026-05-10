#pragma once

#include <string>
#include <vector>

#include "system/SystemConfig.hpp"

// Plain-data project. Owned by the DSP thread (DPF state load/save runs DSP-side
// without the UI binary instantiated, e.g. when Renoise loads a project but the
// editor is never opened). reflectcpp-serializable end-to-end.
struct ProjectSettings {
    // Empty placeholder — populated as features land (audio routing, MIDI
    // routing, layout, save policy, zoom, autosave). Reflectcpp serializes
    // even an empty struct, so adding fields later is a non-breaking change
    // as long as defaults survive an absent JSON key.
};

struct ProjectConfig {
    // Locked spelling — bump on breaking schema changes. Treat unknown
    // variant alternatives as forward-compatible no-ops.
    std::string               schemaVersion = "1.0";
    ProjectSettings           settings;
    std::vector<SystemConfig> systems;
};
