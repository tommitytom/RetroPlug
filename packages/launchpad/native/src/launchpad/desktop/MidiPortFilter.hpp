#pragma once

// The test-only MIDI port allow list, `RP_MIDI_PORT_ALLOW`: a comma-separated list of SUBSTRINGS of a port's name, and a
// port may be listed or opened only when its name contains one of them. Unset or empty means every port, so an ordinary
// run is unchanged.
//
// It exists because a check of this pack drives a REAL standalone: tools/run-sdl-controller.sh stands a fake Launchpad up
// on the ALSA sequencer and then runs the app, whose scan sends a Universal Device Inquiry out of every output the system
// lists and whose shared stream opens every input. On a development machine that is somebody's hardware - a mixer's
// ports, a USB MIDI cable, a Launchpad Pro MK3 - and a test must never write to, or take, a device it did not put there.
// Matching is on a substring because the name a backend reports is not known in advance: RtMidi's ALSA names carry the
// client and port numbers ("LPProMK3 MIDI:1 24:0"), and those move as other clients come and go. The harness therefore
// names its fakes by their WHOLE names, client number included, which is what makes an entry it wrote unable to match a
// real device: the fake's client is a name no device has.
//
// The filter is honoured by everything in this package that touches a port: listMidiPorts (what a scan and the port
// pickers enumerate), the RtMidi opener, MidiPortPool and ControllerLink. packages/native/src/host/input/MidiIo.hpp holds
// the same rule for the host's own shared stream, and cannot share this header (a package may include its own tree and
// its declared packages', not the shell's), so the two are kept in step by hand: one exact-spelling SUBSTRING list, read
// from the one variable. retroplug-midi-test runs both bodies over one table of inputs, so the two cannot drift apart
// without a failed case.
//
// The names that are not hardware at all (isHardwareMidiPort below) are the stream's rule too: it drops RetroPlug's own
// virtual ports and ALSA's "Through", and so does this package's list, by the same table case.
//
// It is NOT a safety feature for a user: a product does not set it, and a user who did would only lose ports from their
// own pickers.

#include <cstdlib>
#include <string>
#include <vector>

namespace rp::launchpad {

/** Whether `name` passes the allow list `spec` (RP_MIDI_PORT_ALLOW's text). An empty spec allows everything, as does a
 *  spec of separators alone ("the list I did not mean to write" is not "no ports at all"). An entry matches when the name
 *  CONTAINS it, case-sensitively. */
inline bool midiPortAllowedBy(const std::string& name, const std::string& spec) {
    std::string entry;
    std::string::size_type at = 0;
    bool any = false;
    while (at <= spec.size()) {
        const std::string::size_type comma = spec.find(',', at);
        entry = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        // Trim the spaces around an entry, so "A, B" reads as two names rather than one that starts with a space.
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

/** `midiPortAllowedBy(name, RP_MIDI_PORT_ALLOW)`. Read on every call, so a test can set it up and take it down around a
 *  case; the callers are a port listing or an open, never the audio thread. */
inline bool midiPortAllowed(const std::string& name) {
    const char* spec = std::getenv("RP_MIDI_PORT_ALLOW");
    return midiPortAllowedBy(name, spec ? spec : "");
}

/** Whether `name` is a hardware port at all: false for a RetroPlug virtual port and for ALSA's "Midi Through", a
 *  loopback with no device behind it. It covers three names a scan must never write a Device Inquiry into: the shared
 *  stream's own `<client>` ports, this pack's `RetroPlug Launchpad:<device>` link ports (so a second instance's claimed
 *  pair is not taken for a device), and Through. The shared stream drops the same two in MidiIo.hpp's isHardwareMidiPort;
 *  a package may not include that tree, so `retroplug-midi-test` runs both bodies over one table, as it does the allow
 *  list's. The pack's opener names its client "RetroPlug Launchpad", and passing "RetroPlug" here drops its ports too. */
inline bool isHardwareMidiPort(const std::string& name, const std::string& clientName) {
    if (name.find(clientName) != std::string::npos) return false;  // a RetroPlug virtual port
    if (name.find("Through") != std::string::npos) return false;    // ALSA MIDI-through (no hardware)
    return true;
}

/** The names in `names` that are hardware ports and pass `midiPortAllowed`, in order: the listing a port picker or a
 *  scan reads. `clientName` is the stream's own client name ("RetroPlug"), which this pack's own link ports contain as
 *  well, so one name covers both. */
inline std::vector<std::string> allowedMidiPorts(const std::vector<std::string>& names,
                                                 const std::string& clientName = "RetroPlug") {
    std::vector<std::string> out;
    for (const std::string& name : names)
        if (isHardwareMidiPort(name, clientName) && midiPortAllowed(name)) out.push_back(name);
    return out;
}

}  // namespace rp::launchpad
