#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"

// JSON shapes for the per-user config tree. Layout mirrors
// src/project/ProjectSerialization.hpp.
//
// Two files live on disk:
//   <config dir>/config.json            <-- UserConfigJson
//   <config dir>/bindings/<name>.json   <-- BindingMapJson
//
// `UserConfigDto` is the merged form shipped over RPC and held in memory.

struct UserConfigJson {
    int          schemaVersion          = 1;
    // Two independent active profiles. Keyboard input picks bindings from
    // `bindings/<activeKeyboardBindings>.json`'s `.keyboard` block;
    // gamepad input from `<activeGamepadBindings>.json`'s `.gamepad`.
    std::string  activeKeyboardBindings = "default";
    std::string  activeGamepadBindings  = "default";
    // Default zoom level for fresh projects (1..6). Per-project zoom set
    // via the menu overrides this; see ProjectSettings::zoom.
    std::uint8_t defaultZoom            = 3;
    // Auto-save cartridge battery RAM to the sibling `<rom>.sav` while playing
    // (a global, sticky preference toggled from the Settings menu). Only
    // affects systems loaded from a path. See system/SramAutoSave.hpp.
    bool         autoSaveSram           = false;
};

// Key = GameboyButton name ("Right" "Left" "Up" "Down" "A" "B" "Select" "Start").
// Value = list of symbolic key (or SDL button) names. Multi-bind on purpose.
struct BindingMapJson {
    int                                             schemaVersion = 1;
    std::string                                     name          = "default";
    std::map<std::string, std::vector<std::string>> keyboard;
    std::map<std::string, std::vector<std::string>> gamepad;
};

// Snapshot used both as the RPC payload and the in-memory state. `bindings`
// is synthesized: its `.keyboard` map comes from the active keyboard
// profile, `.gamepad` from the active gamepad profile.
struct UserConfigDto {
    std::string              activeKeyboardBindings;
    std::string              activeGamepadBindings;
    BindingMapJson           bindings;
    std::vector<std::string> availableProfiles;
    std::uint8_t             defaultZoom = 3;
    bool                     autoSaveSram = false;
};

inline std::string userConfigToJson(const UserConfigJson& cfg) {
    return rfl::json::write(cfg);
}

inline std::optional<UserConfigJson> userConfigFromJson(std::string_view json) {
    auto r = rfl::json::read<UserConfigJson>(json);
    if (!r) return std::nullopt;
    return std::move(r.value());
}

inline std::string bindingMapToJson(const BindingMapJson& b) {
    return rfl::json::write(b);
}

inline std::optional<BindingMapJson> bindingMapFromJson(std::string_view json) {
    auto r = rfl::json::read<BindingMapJson>(json);
    if (!r) return std::nullopt;
    return std::move(r.value());
}

// The hardcoded JS defaults at runtime/lvgljs/input.ts mirrored as JSON.
// Single source of truth used by first-run write-out, by the in-memory
// fallback before any file has been read, and by the Catch2 round-trip.
inline BindingMapJson defaultBindingMap() {
    BindingMapJson b;
    b.name = "default";

    b.keyboard["Right"]  = {"Right"};
    b.keyboard["Left"]   = {"Left"};
    b.keyboard["Up"]     = {"Up"};
    b.keyboard["Down"]   = {"Down"};
    b.keyboard["A"]      = {"Z", "z"};
    b.keyboard["B"]      = {"X", "x"};
    b.keyboard["Start"]  = {"Enter"};
    b.keyboard["Select"] = {"ShiftL", "ShiftR", "Backspace"};

    b.gamepad["Right"]   = {"dpright"};
    b.gamepad["Left"]    = {"dpleft"};
    b.gamepad["Up"]      = {"dpup"};
    b.gamepad["Down"]    = {"dpdown"};
    b.gamepad["A"]       = {"a"};
    b.gamepad["B"]       = {"b"};
    b.gamepad["Start"]   = {"start"};
    b.gamepad["Select"]  = {"back"};

    return b;
}
