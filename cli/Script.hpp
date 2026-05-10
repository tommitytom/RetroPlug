#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "system/InputTypes.hpp"

// JSON-driven scripted input + render parameters for retroplug-cli.
//
// Two forms are accepted per event:
//   {"at_ms": N, "button": "A", "down": true|false}    (explicit)
//   {"at_ms": N, "tap": "A", "hold_ms": 50}            (shorthand)
//
// Validation rejects events that mix or omit both forms.
struct ScriptEvent {
    std::uint32_t              at_ms = 0;
    std::optional<std::string> button;
    std::optional<bool>        down;
    std::optional<std::string> tap;
    std::optional<std::uint32_t> hold_ms;
};

struct Script {
    std::string                rom;
    std::uint32_t              duration_ms = 0;
    std::uint32_t              sample_rate = 44100;
    std::uint32_t              block_size  = 1024;
    std::optional<std::string> out_wav;
    std::vector<ScriptEvent>   events;
};

struct TimedButton {
    std::uint64_t sample;
    GameboyButton button;
    bool          down;
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

// Flatten ScriptEvents into a sorted vector of TimedButton transitions
// (sample-offset, button, down). Performs validation and `tap` expansion.
inline std::vector<TimedButton> flattenEvents(const std::vector<ScriptEvent>& events,
                                              std::uint32_t sampleRate) {
    std::vector<TimedButton> out;
    out.reserve(events.size() * 2);

    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        const bool hasButton = e.button.has_value();
        const bool hasTap    = e.tap.has_value();

        if (hasButton && hasTap)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " has both 'button' and 'tap'");
        if (!hasButton && !hasTap)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " has neither 'button' nor 'tap'");

        if (hasButton) {
            if (!e.down.has_value())
                throw std::runtime_error("event #" + std::to_string(i) +
                                         " is missing 'down'");
            out.push_back({toSample(e.at_ms), parseButtonName(*e.button), *e.down});
        } else {
            const std::uint32_t hold = e.hold_ms.value_or(50);
            const auto btn = parseButtonName(*e.tap);
            out.push_back({toSample(e.at_ms),         btn, true});
            out.push_back({toSample(e.at_ms + hold),  btn, false});
        }
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TimedButton& a, const TimedButton& b) {
                         return a.sample < b.sample;
                     });
    return out;
}
