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
//   {"at_ms": N, "chord": ["Select", "Up"], "system": 0}          (modifier + key)
//   {"at_ms": N, "midi": [144, 60, 100]}                          (routed)
//   {"at_ms": N, "screenshot": "boot", "system": 0}               (PNG dump)
//   {"at_ms": N, "set_transport": true|false}                     (host xport)
//   {"at_ms": N, "set_bpm": 140.0}                                (host tempo)
//
// `system` defaults to 0. For `midi` events it is ignored — MIDI is routed
// through `Project::dispatchMidi` using the project-level `midi_routing`
// mode so it behaves the same way as the plugin.
//
// The `chord` form encodes the LSDJ chord-timing rule: the modifier (first
// element) must be pressed ~200 ms BEFORE the key (second element), otherwise
// LSDJ may miss the chord. Optional `stagger_ms` (default 200) controls the
// gap; `hold_ms` (default 200) controls how long the key is held. Expands to
//   modifier.down @ at_ms
//   key.down      @ at_ms + stagger_ms
//   key.up        @ at_ms + stagger_ms + hold_ms
//   modifier.up   @ at_ms + 2*stagger_ms + hold_ms
// — exactly what works reliably for SELECT+CURSOR and A+CURSOR.
//
// Top-level forms:
//   - Legacy single-system: `rom` at top level.
//   - Multi-system: `systems: [{ rom, link_group }, ...]`. Same nonzero
//     `link_group` puts instances into a shared LinkGroup (lockstep serial).

// LSDJ kit-patch sample spec. One per slot in the kit.
//   path:   filesystem path to a miniaudio-decodable WAV / MP3 / FLAC.
//   name:   3-char (max) uppercase name shown in LSDJ.
struct ScriptKitSample {
    std::string path;
    std::string name;
};

// Top-level form for a `patch_kit` event. Resolves to a synchronous
// kit-compile + role->queuePatch call from the CLI's event drain. Output
// is the same 16 KB bank a UI compileAndPatchKit would produce.
struct ScriptKitPatch {
    std::uint8_t                slot = 0;       // 0..15
    std::string                 name;           // up to 6-char kit name
    std::vector<ScriptKitSample> samples;
};

struct ScriptEvent {
    std::uint32_t                            at_ms = 0;
    std::optional<std::string>               button;
    std::optional<bool>                      down;
    std::optional<std::string>               tap;
    std::optional<std::uint32_t>             hold_ms;
    std::optional<std::vector<std::string>>  chord;       // 2 buttons: [modifier, key]
    std::optional<std::uint32_t>             stagger_ms;  // chord only; default 200
    std::optional<std::vector<std::uint8_t>> midi;
    std::optional<std::string>               screenshot;
    std::optional<std::uint32_t>             system;
    std::optional<bool>                      set_transport;  // simulate host start/stop
    std::optional<double>                    set_bpm;        // simulate host tempo change
    std::optional<ScriptKitPatch>            patch_kit;
};

struct ScriptSystem {
    std::string                  rom;
    std::optional<std::uint8_t>  link_group;     // 0 / unset = standalone
    // Pre-set the LSDJ sync mode for this system, bypassing the sniffer
    // default (MidiSync). Accepts the name of any LsdjSyncMode enumerator
    // ("Off", "MidiSync", "MidiSyncArduinoboy", "MidiMap", "Keyboard",
    // "KeyboardMidi", "MidiPassthrough", "ArduinoboyMaster"). Only meaningful
    // when the ROM is sniffed as LSDJ; ignored otherwise.
    std::optional<std::string>   lsdj_sync_mode;
    // GBA-only: path to a real GBA BIOS file (16384 bytes; filename can be
    // anything — Mesen requires it to be named gba_bios.bin so GbaSystem
    // copies it into its firmware folder on activate). Without it most ROMs
    // hang at the first BIOS SWI; the Cult-of-GBA open-source BIOS works as
    // a drop-in for the smoke test. Ignored for SameBoy / NES ROMs.
    std::optional<std::string>   bios_path;
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
    // Initial host transport state, fed into AudioBlockInfo each block until a
    // `set_transport` / `set_bpm` event changes it. The CLI advances ppqPos
    // internally so LsdjSyncRole sees the same tempo the DAW would.
    std::optional<double>                     bpm;                // default 120
    std::optional<bool>                       transport_running;  // default false
    std::vector<ScriptEvent>                  events;
};

struct TimedButton {
    std::uint64_t sample;
    std::uint32_t systemIndex;
    std::uint8_t  button;   // SameBoy: GameboyButton; Mesen: NesButton; Gba: GbaButton (cast)
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

// Simulated host-transport edit applied before a block begins. The render
// loop drains these in `[blockStartSample, blockEndSample)` order and updates
// the local cliTransport / cliBpm state used to populate AudioBlockInfo.
struct TimedTransport {
    std::uint64_t        sample;
    std::optional<bool>  setTransport;
    std::optional<double> setBpm;
};

// Returns a button opcode as a raw uint8_t. GameboyButton, NesButton, and
// GbaButton all use the same position-aligned encoding (Right=0..Start=7)
// for the shared eight buttons, so a single name table works for any
// system kind; the receiving system reinterprets the byte. L and R are
// GBA-only (wire bytes 8 and 9 from GbaButton); pressing them at a
// SameBoy or Mesen system is a no-op (their toXButton switches return A
// for unknown values).
inline std::uint8_t parseButtonName(const std::string& s) {
    std::string lower(s.size(), '\0');
    std::transform(s.begin(), s.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "right")  return static_cast<std::uint8_t>(GameboyButton::Right);
    if (lower == "left")   return static_cast<std::uint8_t>(GameboyButton::Left);
    if (lower == "up")     return static_cast<std::uint8_t>(GameboyButton::Up);
    if (lower == "down")   return static_cast<std::uint8_t>(GameboyButton::Down);
    if (lower == "a")      return static_cast<std::uint8_t>(GameboyButton::A);
    if (lower == "b")      return static_cast<std::uint8_t>(GameboyButton::B);
    if (lower == "select") return static_cast<std::uint8_t>(GameboyButton::Select);
    if (lower == "start")  return static_cast<std::uint8_t>(GameboyButton::Start);
    if (lower == "l")      return static_cast<std::uint8_t>(GbaButton::L);
    if (lower == "r")      return static_cast<std::uint8_t>(GbaButton::R);
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
// name found ("button", "tap", "chord", "midi", "screenshot", "transport")
// so the caller can branch without re-checking the optionals. A "transport"
// event may carry either or both of set_transport / set_bpm.
inline const char* validateEventForm(const ScriptEvent& e, std::size_t index) {
    const bool isTransport = e.set_transport.has_value() || e.set_bpm.has_value();
    const int n = (e.button.has_value()     ? 1 : 0)
                + (e.tap.has_value()        ? 1 : 0)
                + (e.chord.has_value()      ? 1 : 0)
                + (e.midi.has_value()       ? 1 : 0)
                + (e.screenshot.has_value() ? 1 : 0)
                + (e.patch_kit.has_value()  ? 1 : 0)
                + (isTransport              ? 1 : 0);
    if (n == 0)
        throw std::runtime_error("event #" + std::to_string(index) +
                                 " has no 'button'/'tap'/'chord'/'midi'/'screenshot'/'patch_kit'/'set_transport'/'set_bpm'");
    if (n > 1)
        throw std::runtime_error("event #" + std::to_string(index) +
                                 " mixes multiple input forms");
    if (e.button)     return "button";
    if (e.tap)        return "tap";
    if (e.chord)      return "chord";
    if (e.midi)       return "midi";
    if (e.screenshot) return "screenshot";
    if (e.patch_kit)  return "patch_kit";
    return "transport";
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
        const std::string_view form = validateEventForm(e, i);
        if (form != "button" && form != "tap" && form != "chord")
            continue;

        const std::uint32_t sysIdx = e.system.value_or(0);
        if (sysIdx >= systemCount)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'system' index " + std::to_string(sysIdx) +
                                     " is out of range (have " +
                                     std::to_string(systemCount) + ")");

        if (form == "button") {
            if (!e.down.has_value())
                throw std::runtime_error("event #" + std::to_string(i) +
                                         " 'button' form requires 'down'");
            out.push_back({toSample(e.at_ms), sysIdx, parseButtonName(*e.button), *e.down});
        } else if (form == "tap") {
            const std::uint32_t hold = e.hold_ms.value_or(50);
            const auto btn = parseButtonName(*e.tap);
            out.push_back({toSample(e.at_ms),         sysIdx, btn, true});
            out.push_back({toSample(e.at_ms + hold),  sysIdx, btn, false});
        } else { // "chord"
            if (e.chord->size() != 2)
                throw std::runtime_error("event #" + std::to_string(i) +
                                         " 'chord' must have exactly 2 buttons (modifier, key)");
            const std::uint32_t stagger = e.stagger_ms.value_or(200);
            const std::uint32_t hold    = e.hold_ms.value_or(200);
            const auto mod = parseButtonName((*e.chord)[0]);
            const auto key = parseButtonName((*e.chord)[1]);
            // Modifier down, then key down, key up, modifier up.
            out.push_back({toSample(e.at_ms),                          sysIdx, mod, true});
            out.push_back({toSample(e.at_ms + stagger),                sysIdx, key, true});
            out.push_back({toSample(e.at_ms + stagger + hold),         sysIdx, key, false});
            out.push_back({toSample(e.at_ms + 2 * stagger + hold),     sysIdx, mod, false});
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

// patch_kit events trigger a synchronous kit-compile + role-patch on the
// target system at their `at_ms`. We flatten timing here so the driver
// walks them in order alongside the rest of the event forms; the compile
// itself (~50 ms for a 16-sample kit) happens inline on the event drain.
struct TimedKitPatch {
    std::uint64_t  sample;
    std::uint32_t  systemIndex;
    ScriptKitPatch patch;
};

inline std::vector<TimedKitPatch> flattenKitPatches(const std::vector<ScriptEvent>& events,
                                                    std::uint32_t sampleRate,
                                                    std::uint32_t systemCount) {
    std::vector<TimedKitPatch> out;
    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        if (!e.patch_kit.has_value()) continue;
        const std::uint32_t sysIdx = e.system.value_or(0);
        if (sysIdx >= systemCount)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'system' index " + std::to_string(sysIdx) +
                                     " is out of range");
        if (e.patch_kit->slot >= 16)
            throw std::runtime_error("event #" + std::to_string(i) +
                                     " 'patch_kit.slot' must be 0..15");
        out.push_back({toSample(e.at_ms), sysIdx, *e.patch_kit});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TimedKitPatch& a, const TimedKitPatch& b) {
                         return a.sample < b.sample;
                     });
    return out;
}

inline std::vector<TimedTransport> flattenTransport(const std::vector<ScriptEvent>& events,
                                                    std::uint32_t sampleRate) {
    std::vector<TimedTransport> out;
    auto toSample = [sampleRate](std::uint32_t ms) -> std::uint64_t {
        return (static_cast<std::uint64_t>(ms) * sampleRate) / 1000u;
    };

    for (const auto& e : events) {
        if (!e.set_transport.has_value() && !e.set_bpm.has_value()) continue;
        out.push_back({toSample(e.at_ms), e.set_transport, e.set_bpm});
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TimedTransport& a, const TimedTransport& b) {
                         return a.sample < b.sample;
                     });
    return out;
}
