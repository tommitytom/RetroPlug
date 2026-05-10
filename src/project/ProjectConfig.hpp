#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "system/SystemConfig.hpp"

// Plain-data project. Owned by the DSP thread (DPF state load/save runs DSP-side
// without the UI binary instantiated, e.g. when Renoise loads a project but the
// editor is never opened). reflectcpp-serializable end-to-end.

// Tile arrangement for multi-instance display. The UI implements `Auto` as
// a count-based heuristic (1=center, 2=row, 3-4=2x2, 5-9=3x3, 10-16=4x4).
enum class SystemLayout : std::uint8_t {
    Auto   = 0,
    Row    = 1,
    Column = 2,
    Grid   = 3,
};

struct ProjectSettings {
    SystemLayout layout = SystemLayout::Auto;
    // Empty placeholder for additions — audio routing, MIDI routing, save
    // policy, zoom, autosave. Reflectcpp serializes even an empty struct,
    // so adding fields later is a non-breaking change as long as defaults
    // survive an absent JSON key.
};

struct ProjectConfig {
    // Locked spelling — bump on breaking schema changes. Treat unknown
    // variant alternatives as forward-compatible no-ops.
    std::string               schemaVersion = "1.0";
    ProjectSettings           settings;
    std::vector<SystemConfig> systems;
};
