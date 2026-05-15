#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "project/ProjectConfig.hpp"
#include "system/InputTypes.hpp"
#include "transport/MidiTypes.hpp"

// JSON-driven scripted input + render parameters for retroplug-cli.
//
// Event forms (mutually exclusive — exactly one must be set):
//   {"at_ms": N, "button": "A", "down": true|false, "system": 0}  (explicit)
//   {"at_ms": N, "tap": "A", "hold_ms": 50, "system": 0}          (shorthand)
//   {"at_ms": N, "midi": [144, 60, 100]}                          (routed)
//   {"at_ms": N, "screenshot": "boot", "system": 0}               (PNG dump)
//
// `system` defaults to 0. For `midi` events it is ignored — MIDI is routed
// through `Project::dispatchMidi` using the project-level `midi_routing`
// mode so it behaves the same way as the plugin.
//
// Top-level forms:
//   - Legacy single-system: `rom` at top level.
//   - Multi-system: `systems: [{ rom, link_group }, ...]`. Same nonzero
//     `link_group` puts instances into a shared LinkGroup (lockstep serial).

struct ScriptEvent {
    std::uint32_t                       at_ms = 0;
    std::optional<std::string>          button;
    std::optional<bool>                 down;
    std::optional<std::string>          tap;
    std::optional<std::uint32_t>        hold_ms;
    std::optional<std::vector<std::uint8_t>> midi;
    std::optional<std::string>          screenshot;
    std::optional<std::uint32_t>        system;
};

struct ScriptSystem {
    std::string                  rom;
    std::optional<std::uint8_t>  link_group;   // 0 / unset = standalone
};

struct Script {
    // Legacy single-system field. If non-empty AND `systems` is empty/unset,
    // it is promoted to a one-element `systems` array at load time. Optional
    // so multi-system scripts can omit it cleanly.
    std::optional<std::string>                rom;
    std::optional<std::vector<ScriptSystem>>  systems;
    std::optional<std::string>                midi_routing;
    std::uint32_t                             duration_ms = 0;
    std::uint32_t                             sample_rate = 44100;
    std::uint32_t                             block_size  = 1024;
    std::optional<std::string>                out_wav;
    std::vector<ScriptEvent>                  events;
};

struct TimedButton {
    std::uint64_t sample;
    std::uint32_t systemIndex;
    GameboyButton button;
    bool          down;
};

struct TimedMidi {
    std::uint64_t sample;
    MidiEvent     event;
};

struct TimedScreenshot {
    std::uint64_t sample;
    std::uint32_t systemIndex;
    std::string   name;
};

inline GameboyButton parseButtonName(const std::string& s) {
    std::string lower(s.size(), '\0');
    std::transform(s.begin(), s.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "right")  return GameboyButton::Right;
    if (lower == "left")   return GameboyButton::Left;
    if (lower == "up")     return GameboyButton::Up;
    if (lower == "down")   return GameboyButton::Down;
    if (lower == "a")      return GameboyButton::A;
    if (lower == "b")      return GameboyButton::B;
    if (lower == "select") return GameboyButton::Select;
    if (lower == "start")  return GameboyButton::Start;
    throw std::runtime_error("unknown button name: " + s);
}

inline MidiRouting parseMidiRouting(const std::string& s) {
    if (s == "SendToAll")               return MidiRouting::SendToAll;
    if (s == "FourChannelsPerInstance") return MidiRouting::FourChannelsPerInstance;
    if (s == "OneChannelPerInstance")   return MidiRouting::OneChannelPerInstance;
    if (s == "MidiChannelToInstance")   return MidiRouting::MidiChannelToInstance;
    throw std::runtime_error("unknown midi_routing: " + s);
}

// Reject events that don't have exactly one input form. Returns the form
// name found ("button", "tap", "midi", "screenshot") so the caller can
// branch without re-checking the optionals.
inline const char* validateEventForm(const ScriptEvent& e, std::size_t index) {
    const int n = (e.button.has_value()     ? 1 : 0)
                + (e.tap.has_value()        ? 1 : 0)
                + (e.midi.has_value()       ? 1 : 0)
                + (e.screenshot.has_value() ? 1 : 0);
    if (n == 0)
        throw std::runtime_error("event #" + std::to_string(index) +
                                 " has no 'button'/'tap'/'midi'/'screenshot'");
    if (n > 1)
        throw std::runtime_error("event #" + std::to_string(index) +
                                 " mixes multiple input forms");
    if (e.button)     return "button";
    if (e.tap)        return "tap";
    if (e.midi)       return "midi";
    return "screenshot";
}

// Flatten button/tap events into a sorted vector of TimedButton transitions
// keyed by (sample, systemIndex). MIDI and screenshot events are skipped
// (they have their own flatten passes). `systemCount` bounds the `system`
// index check; pass 1 for legacy scripts.
inline std::vector<TimedButton> flattenEvents(const std::vector<ScriptEvent>& events,
                                              std::uint32_t sampleRate,
                                              std::uint32_t systemCount) {
    std::vector<TimedButton> out;
    out.reserve(events.size() * 2);

    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        const char* form = validateEventForm(e, i);
        if (std::string_view(form) != "button" && std::string_view(form) != "tap")
            continue;

        const std::uint32_t sysIdx = e.system.value_or(0);
        if (sysIdx >= systemCount)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'system' index " + std::to_string(sysIdx) +
                                     " is out of range (have " +
                                     std::to_string(systemCount) + ")");

        if (std::string_view(form) == "button") {
            if (!e.down.has_value())
                throw std::runtime_error("event #" + std::to_string(i) +
                                         " 'button' form requires 'down'");
            out.push_back({toSample(e.at_ms), sysIdx, parseButtonName(*e.button), *e.down});
        } else {
            const std::uint32_t hold = e.hold_ms.value_or(50);
            const auto btn = parseButtonName(*e.tap);
            out.push_back({toSample(e.at_ms),         sysIdx, btn, true});
            out.push_back({toSample(e.at_ms + hold),  sysIdx, btn, false});
        }
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TimedButton& a, const TimedButton& b) {
                         return a.sample < b.sample;
                     });
    return out;
}

inline std::vector<TimedMidi> flattenMidi(const std::vector<ScriptEvent>& events,
                                          std::uint32_t sampleRate) {
    std::vector<TimedMidi> out;
    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        if (!e.midi.has_value()) continue;
        const auto& bytes = *e.midi;
        if (bytes.empty() || bytes.size() > MidiEvent::kDataSize)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'midi' must have 1.." +
                                     std::to_string(MidiEvent::kDataSize) + " bytes");
        MidiEvent ev;
        ev.frame = 0;
        ev.size  = static_cast<std::uint32_t>(bytes.size());
        for (std::size_t b = 0; b < bytes.size(); ++b) ev.data[b] = bytes[b];
        out.push_back({toSample(e.at_ms), ev});
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TimedMidi& a, const TimedMidi& b) {
                         return a.sample < b.sample;
                     });
    return out;
}

inline std::vector<TimedScreenshot> flattenScreenshots(const std::vector<ScriptEvent>& events,
                                                      std::uint32_t sampleRate,
                                                      std::uint32_t systemCount) {
    std::vector<TimedScreenshot> out;
    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        if (!e.screenshot.has_value()) continue;
        const std::uint32_t sysIdx = e.system.value_or(0);
        if (sysIdx >= systemCount)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'system' index " + std::to_string(sysIdx) +
                                     " is out of range (have " +
                                     std::to_string(systemCount) + ")");
        if (e.screenshot->empty())
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'screenshot' name must be non-empty");
        out.push_back({toSample(e.at_ms), sysIdx, *e.screenshot});
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TimedScreenshot& a, const TimedScreenshot& b) {
                         return a.sample < b.sample;
                     });
    return out;
}
