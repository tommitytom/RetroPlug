#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "system/InputTypes.hpp"
#include "transport/MidiTypes.hpp"

// JSON-driven scripted input + render parameters for retroplug-cli.
//
// Three forms are accepted per event:
//   {"at_ms": N, "button": "A", "down": true|false}    (explicit button)
//   {"at_ms": N, "tap": "A", "hold_ms": 50}            (button shorthand)
//   {"at_ms": N, "midi": [144, 60, 100]}               (raw MIDI bytes, 1..4)
//
// Validation rejects events that mix forms or set none.
struct ScriptEvent {
    std::uint32_t                       at_ms = 0;
    std::optional<std::string>          button;
    std::optional<bool>                 down;
    std::optional<std::string>          tap;
    std::optional<std::uint32_t>        hold_ms;
    std::optional<std::vector<std::uint8_t>> midi;
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

struct TimedMidi {
    std::uint64_t sample;
    MidiEvent     event;
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
        const bool hasMidi   = e.midi.has_value();

        // MIDI events are flattened separately; ignore them here.
        if (hasMidi && !hasButton && !hasTap) continue;

        if (hasButton && hasTap)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " has both 'button' and 'tap'");
        if (!hasButton && !hasTap)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " has neither 'button' nor 'tap' nor 'midi'");

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
