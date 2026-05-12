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

// How host MIDI is split across multiple instances. Modes that map a
// channel onto a single instance wrap with `% N`, so a project with 5+
// instances stays well-defined under FourChannelsPerInstance.
//   SendToAll               broadcast every event to every system; channel preserved.
//   FourChannelsPerInstance instance N receives channels (4N+1)..(4N+4) (1-indexed).
//   OneChannelPerInstance   instance N receives only channel N+1 (1-indexed).
//   MidiChannelToInstance   like OneChannelPerInstance but channel is rewritten to 1.
enum class MidiRouting : std::uint8_t {
    SendToAll               = 0,
    FourChannelsPerInstance = 1,
    OneChannelPerInstance   = 2,
    MidiChannelToInstance   = 3,
};

struct ProjectSettings {
    SystemLayout layout       = SystemLayout::Auto;
    MidiRouting  midiRouting  = MidiRouting::SendToAll;
};

struct ProjectConfig {
    // Locked spelling — bump on breaking schema changes. Treat unknown
    // variant alternatives as forward-compatible no-ops.
    std::string               schemaVersion = "1.0";
    ProjectSettings           settings;
    std::vector<SystemConfig> systems;
};
