#pragma once

// The host's MIDI port rules (docs/modular-graph-phase-f5b-design.md sections 4.1 and 4.5), the ONE copy: which names
// are hardware at all, which the test-only allow list hides, and which ports a selection opens. The stream and the
// Launchpad pack both read them here, so a port a pack's scan would offer and a port the host would open are the same
// rule, and this package's test holds that rule on whichever platform builds it.
//
// Everything here is pure: a plain vector of names in, indices out. The backend's listing is unfiltered and these
// decide, so the fake can be tested against the same rule the real system is held to.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace rp::midi {

/** Whether a control-surface SCAN's held edge means anything for this stream: true only where a MIDI input is EXCLUSIVE
 *  (Windows), because there the stream has to let one go for the scan to hear it. An ALSA sequencer input and a
 *  CoreMIDI endpoint are shared - the scan opens its own connection and hears everything the stream does - so closing
 *  the stream's inputs would cost the user their MIDI for the scan's seconds and buy nothing. The pack asks every
 *  product the same question (rp_device_host::suspend_hardware_inputs) and the SDL answers it by this; a function
 *  rather than a constant, so the rule is held on whichever platform builds it. */
inline bool scanSuspendsInputs() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

/** Whether `name` passes the allow list `spec` (RP_MIDI_PORT_ALLOW's text): a comma-separated list of SUBSTRINGS a
 *  port's name must contain. An empty spec (or one of separators alone) allows every port, so an ordinary run is
 *  unchanged.
 *
 *  The list is TEST-ONLY, and it is what keeps a check off real hardware: tools/run-sdl-controller.sh drives a real
 *  standalone against a fake Launchpad on the ALSA sequencer, and a stream on "All Devices" would otherwise open every
 *  MIDI input the machine has, a mixer's and a USB cable's included. The harness names its fakes by their WHOLE port
 *  names, client number included, so an entry it wrote cannot match a real device. A product never sets it. */
inline bool midiPortAllowedBy(const std::string& name, const std::string& spec) {
    std::string entry;
    std::string::size_type at = 0;
    bool any = false;
    while (at <= spec.size()) {
        const std::string::size_type comma = spec.find(',', at);
        entry = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        const std::string::size_type first = entry.find_first_not_of(" \t");
        const std::string::size_type last  = entry.find_last_not_of(" \t");
        entry = first == std::string::npos ? std::string() : entry.substr(first, last - first + 1);
        if (!entry.empty()) {
            any = true;
            if (name.find(entry) != std::string::npos) return true;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return !any;
}

/** `midiPortAllowedBy(name, RP_MIDI_PORT_ALLOW)`. Read on every call: the callers list or open a port, never the audio
 *  thread, so a test can set the filter up and take it down around a case. */
inline bool midiPortAllowed(const std::string& name) {
    const char* spec = std::getenv("RP_MIDI_PORT_ALLOW");
    return midiPortAllowedBy(name, spec ? spec : "");
}

/** Whether `name` is a hardware port at all: false for RetroPlug's own virtual ports (a name containing `clientName`,
 *  which "RetroPlug" also matches in the Launchpad pack's "RetroPlug Launchpad:<device>" link ports, so a second
 *  instance's claimed pair is never taken for a device) and for ALSA's "Midi Through", a loopback with no device behind
 *  it. One predicate, so every list and every open set agrees on which names exist. */
inline bool isHardwareMidiPort(const std::string& name, const std::string& clientName) {
    if (name.find(clientName) != std::string::npos) return false;  // our own virtual port
    if (name.find("Through") != std::string::npos) return false;    // ALSA MIDI-through (no hardware)
    return true;
}

/** Indices of every hardware port in `names` (skipping our own virtual port, ALSA "Through", `reserved`, and any port
 *  the test-only allow list hides - so a filtered process neither lists nor opens it, whichever selection asks). */
inline std::vector<std::size_t> hardwarePortIndices(const std::vector<std::string>& names,
                                                   const std::string& clientName, const std::string& reserved = {}) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!isHardwareMidiPort(names[i], clientName)) continue;  // our own virtual port, or ALSA's Through
        if (!reserved.empty() && names[i] == reserved) continue;   // claimed by a control surface
        if (!midiPortAllowed(names[i])) continue;                  // hidden by RP_MIDI_PORT_ALLOW
        out.push_back(i);
    }
    return out;
}

/** Index of the hardware port named exactly `selection`, or none (empty selection / no match / a skipped port). Exact
 *  because the caller picked the name out of the very list this searched - the name matching that survives a move to
 *  another machine lives in TypeScript, which hands this an exact name. */
inline std::optional<std::size_t> matchPortIndex(const std::vector<std::string>& names, const std::string& clientName,
                                                 const std::string& selection, const std::string& reserved = {}) {
    if (selection.empty()) return std::nullopt;
    for (std::size_t i : hardwarePortIndices(names, clientName, reserved))
        if (names[i] == selection) return i;
    return std::nullopt;
}

/** The input selection meaning "every hardware port at once". An explicit CHOICE, not the default: opening every device
 *  turns out to be a surprising thing to do by default, because a device plugged in for one purpose is then also a MIDI
 * source - a control surface's free-running clock ends up driving the host tempo, a controller's mixer ports send notes
 * at the cart. The stream's own selection is a list plus an `all` flag; this is the spelling a saved or persisted
 * single selection uses for it (the CLI facet's "*"; a project's section carries the list and its flag instead). */
inline constexpr const char* kAllInputs = "*";

/** Indices of the input ports to open for a selection of `names`: none when the selection is empty and `all` is false
 *  (the default), every hardware port for `all`, else each port whose name is one of `selected`, exactly. A port
 *  `reserved` by a control surface is skipped in every case, and a stream whose inputs are `suspended` (a
 *  control-surface scan holds them, on the one platform where a scan needs it) opens nothing at all.
 *
 *  Pure so the policy itself is testable (retroplug-midi-test) rather than only observable by watching which ports a
 *  running standalone happens to open. */
inline std::vector<std::size_t> inputPortsToOpen(const std::vector<std::string>& names, const std::string& clientName,
                                                 const std::vector<std::string>& selected, bool all,
                                                 const std::string& reserved = {}, bool suspended = false) {
    if (suspended) return {};
    const std::vector<std::size_t> hardware = hardwarePortIndices(names, clientName, reserved);
    if (all) return hardware;
    std::vector<std::size_t> open;
    for (std::size_t i : hardware) {
        for (const std::string& name : selected) {
            if (!name.empty() && names[i] == name) {
                open.push_back(i);
                break;
            }
        }
    }
    return open;
}

/** Pull the System Real-Time bytes (0xF8..0xFF: clock, start/continue/stop, reset) out of one incoming message, handing
 *  each to `onRealtime` in order, and leave `bytes` holding whatever else was there.
 *
 *  Real-time bytes are a stream WITHIN the stream: the spec lets them appear at any byte boundary, including between
 *  the data bytes of another message, and a transport is free to hand several events over as one buffer. Matching only
 *  a one-byte message therefore both MISSES clock (a batched pair reads as no clock at all - a tempo estimate built on
 *  those pulses comes out a whole ratio slow) and corrupts what is left (a clock byte staged into the middle of a
 *  NoteOn goes down a cart's link port as noise).
 *
 *  Shrinks in place: this runs per message on the audio thread, so it must not allocate. */
template <typename F>
inline void extractRealtime(std::vector<std::uint8_t>& bytes, F&& onRealtime) {
    std::size_t write = 0;
    for (std::size_t read = 0; read < bytes.size(); ++read) {
        const std::uint8_t b = bytes[read];
        if (b >= 0xF8) onRealtime(b);
        else bytes[write++] = b;
    }
    bytes.resize(write);
}

}  // namespace rp::midi
