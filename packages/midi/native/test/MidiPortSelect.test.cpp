// Unit-tests the pure port-selection rules in midi/PortFilter.hpp (hardwarePortIndices / matchPortIndex /
// inputPortsToOpen / extractRealtime) and the test-only allow list. These decide which hardware ports the stream opens
// for a selection - the whole Settings > MIDI policy, including the two things that make a port NOT open: the input
// default is None (opening every device is an explicit choice), and a port a control surface has claimed is skipped. No
// MIDI system or RtMidi runtime is needed: the helpers operate on a plain vector<string> of port names.
//
// The rule is this package's, and the Launchpad pack reads the same header, so one table holds both callers.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include "midi/PortFilter.hpp"

using rp::midi::hardwarePortIndices;
using rp::midi::inputPortsToOpen;
using rp::midi::isHardwareMidiPort;
using rp::midi::matchPortIndex;
using rp::midi::midiPortAllowed;
using rp::midi::midiPortAllowedBy;

namespace {
/** RP_MIDI_PORT_ALLOW is a developer's environment variable (tools/run-sdl-controller.sh exports it for the
 *  standalone's harness), and a hand run of that harness leaves it in the shell. Every case here but the filter's own
 *  assumes the UNFILTERED answer, so the file clears it on load and puts back whatever it found at exit; a case that
 *  wants a list sets it for its own scope (PortAllowList). */
struct UnfilteredFileScope {
    UnfilteredFileScope() {
        const char* was = std::getenv("RP_MIDI_PORT_ALLOW");
        had_            = was != nullptr;
        if (had_) was_ = was;
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", "");
#else
        unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    ~UnfilteredFileScope() {
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", had_ ? was_.c_str() : "");
#else
        if (had_) setenv("RP_MIDI_PORT_ALLOW", was_.c_str(), 1);
        else unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    bool        had_ = false;
    std::string was_;
};
const UnfilteredFileScope kUnfiltered;

// A realistic ALSA-style port listing: our own virtual port, a MIDI-through (no hardware), and two controllers.
const std::vector<std::string> kPorts = {
    "RetroPlug In",        // 0: our own virtual port (contains the client name) - always skipped
    "Midi Through Port-0", // 1: ALSA MIDI-through - always skipped
    "Launchpad MK2",       // 2: hardware
    "Arturia KeyStep 32",  // 3: hardware
};
const std::string kClient = "RetroPlug";
}  // namespace

TEST_CASE("hardwarePortIndices keeps only the hardware ports (skips own virtual port + Through)") {
    REQUIRE(hardwarePortIndices(kPorts, kClient) == std::vector<std::size_t>{2, 3});
}

TEST_CASE("hardwarePortIndices on an empty / all-skipped list yields nothing") {
    REQUIRE(hardwarePortIndices({}, kClient).empty());
    REQUIRE(hardwarePortIndices({"RetroPlug In", "RetroPlug Out", "Midi Through Port-0"}, kClient).empty());
}

TEST_CASE("matchPortIndex: empty selection never matches (that means the default, resolved by the caller)") {
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "").has_value());
}

TEST_CASE("matchPortIndex: a present hardware device resolves to its index") {
    auto idx = matchPortIndex(kPorts, kClient, "Launchpad MK2");
    REQUIRE(idx.has_value());
    REQUIRE(*idx == 2);
    REQUIRE(*matchPortIndex(kPorts, kClient, "Arturia KeyStep 32") == 3);
}

TEST_CASE("matchPortIndex: an absent device yields nothing (remembered by the caller, re-applied on reconnect)") {
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Some Unplugged Synth").has_value());
}

TEST_CASE("matchPortIndex never selects our own virtual port or a Through port, even by exact name") {
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "RetroPlug In").has_value());
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Midi Through Port-0").has_value());
}

TEST_CASE("selection policy: a named input opens that port, or nothing when it is absent") {
    REQUIRE(matchPortIndex(kPorts, kClient, "Launchpad MK2").value() == 2);
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Ghost").has_value());
}

// The INPUT default is None, not every device. Opening everything is a surprising amount of behaviour to get without
// asking for it: anything plugged in becomes a MIDI source, so a control surface's free-running clock drives the host
// tempo and a controller's mixer ports send notes at the cart. The virtual "<client> In" port is always open
// regardless, so this only makes PHYSICAL devices opt-in.
TEST_CASE("inputPortsToOpen: the default selection opens no hardware at all") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, {}, false).empty());
    REQUIRE(inputPortsToOpen(kPorts, kClient, {""}, false).empty());  // an empty name selects nothing, not a port
}

TEST_CASE("inputPortsToOpen: the explicit all-devices flag opens every hardware port") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, {}, true) == std::vector<std::size_t>{2, 3});
    // ...and the names it was handed are then ignored, so "all" and a stale list cannot disagree.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Ghost"}, true) == std::vector<std::size_t>{2, 3});
}

TEST_CASE("inputPortsToOpen: named devices open exactly those ports, and an absent one opens nothing") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Arturia KeyStep 32"}, false) == std::vector<std::size_t>{3});
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Launchpad MK2", "Arturia KeyStep 32"}, false) ==
            std::vector<std::size_t>{2, 3});
    // A duplicate selection still opens the port once.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Launchpad MK2", "Launchpad MK2"}, false) ==
            std::vector<std::size_t>{2});
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Some Unplugged Synth"}, false).empty());
    // One name that is present and one that is not: the present one opens, the stale one is remembered by the caller.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Ghost", "Launchpad MK2"}, false) == std::vector<std::size_t>{2});
}

TEST_CASE("inputPortsToOpen: a reserved port is skipped whichever way it is selected") {
    // Both paths matter: "all devices" is the case that would merge a Launchpad's pad presses into the musical stream,
    // and naming it explicitly must not smuggle it back in.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {}, true, "Launchpad MK2") == std::vector<std::size_t>{3});
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Launchpad MK2"}, false, "Launchpad MK2").empty());
}

// A port a control surface has claimed exclusively is skipped by every helper. Not tidiness: a pad press is a NoteOn,
// and LSDj's MI.MAP translator reads a NoteOn as a row launch, so a Launchpad sharing the musical stream would fire
// every launch twice - once quantised by the controller app, once raw. "All devices" is the case that would otherwise
// do it.
TEST_CASE("a reserved port drops out of All Devices") {
    const auto open = hardwarePortIndices(kPorts, kClient, "Launchpad MK2");
    REQUIRE(open == std::vector<std::size_t>{3});  // the KeyStep still plays; the surface does not
}

TEST_CASE("a reserved port cannot be selected as the input device either") {
    // Selecting it explicitly must not smuggle it back in - the link owns the OS port, so opening it twice would fail
    // anyway, and silently half-working is worse than not opening it.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Launchpad MK2", "Launchpad MK2").has_value());
    REQUIRE(matchPortIndex(kPorts, kClient, "Arturia KeyStep 32", "Launchpad MK2").value() == 3);
}

TEST_CASE("no reservation is the default and changes nothing") {
    REQUIRE(hardwarePortIndices(kPorts, kClient, "") == hardwarePortIndices(kPorts, kClient));
    // A reserved name that is not present is simply inert (the device was unplugged, the link is down).
    REQUIRE(hardwarePortIndices(kPorts, kClient, "Some Unplugged Launchpad").size() == 2);
}

// --- System Real-Time extraction -------------------------------------------------------------------
// Clock/start/stop are a stream within the stream: the spec allows them at ANY byte boundary, and a transport may hand
// several events over in one buffer. The drain used to recognise them only as a one-byte message, so a batched pair
// counted as no clock at all - which reads downstream as a tempo a whole ratio slow, and puts a stray 0xF8 into
// whatever message it was riding along with.

static std::vector<std::uint8_t> realtimeOf(std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint8_t> seen;
    rp::midi::extractRealtime(bytes, [&](std::uint8_t b) { seen.push_back(b); });
    return seen;
}

TEST_CASE("a lone clock byte is extracted and leaves nothing behind") {
    std::vector<std::uint8_t> msg{0xF8};
    REQUIRE(realtimeOf(msg) == std::vector<std::uint8_t>{0xF8});
    REQUIRE(msg.empty());
}

TEST_CASE("clocks batched into one message are ALL counted") {
    // The halving case: two pulses delivered together used to count as zero.
    std::vector<std::uint8_t> msg{0xF8, 0xF8};
    REQUIRE(realtimeOf(msg) == std::vector<std::uint8_t>{0xF8, 0xF8});
    REQUIRE(msg.empty());
}

TEST_CASE("a clock interleaved INTO another message is pulled out, leaving the message intact") {
    std::vector<std::uint8_t> msg{0x90, 0x3C, 0xF8, 0x40};  // NoteOn with a clock byte mid-message
    REQUIRE(realtimeOf(msg) == std::vector<std::uint8_t>{0xF8});
    REQUIRE(msg == std::vector<std::uint8_t>{0x90, 0x3C, 0x40});
}

TEST_CASE("start / stop / continue and reset all count as real-time; channel data never does") {
    std::vector<std::uint8_t> msg{0xFA, 0x90, 0xFC, 0x3C, 0xFB, 0x40, 0xFF};
    REQUIRE(realtimeOf(msg) == std::vector<std::uint8_t>{0xFA, 0xFC, 0xFB, 0xFF});
    REQUIRE(msg == std::vector<std::uint8_t>{0x90, 0x3C, 0x40});

    std::vector<std::uint8_t> note{0x90, 0x3C, 0x7F};
    REQUIRE(realtimeOf(note).empty());
    REQUIRE(note == std::vector<std::uint8_t>{0x90, 0x3C, 0x7F});
}

TEST_CASE("0xF7 (sysex end) is NOT real-time - the boundary is 0xF8") {
    std::vector<std::uint8_t> msg{0xF0, 0x7E, 0xF7};
    REQUIRE(realtimeOf(msg).empty());
    REQUIRE(msg.size() == 3);
}

// ---- the test-only allow list, and the scan's held edge -------------------------------------------------------

namespace {

/** RP_MIDI_PORT_ALLOW for one case, put back as it was when it goes (the filter reads it on every call). */
struct PortAllowList {
    explicit PortAllowList(const char* spec) {
        const char* was = std::getenv("RP_MIDI_PORT_ALLOW");
        had_            = was != nullptr;
        if (had_) was_ = was;
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", spec);
#else
        setenv("RP_MIDI_PORT_ALLOW", spec, 1);
#endif
    }
    ~PortAllowList() {
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", had_ ? was_.c_str() : "");
#else
        if (had_) setenv("RP_MIDI_PORT_ALLOW", was_.c_str(), 1);
        else unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    PortAllowList(const PortAllowList&)            = delete;
    PortAllowList& operator=(const PortAllowList&) = delete;

    bool        had_ = false;
    std::string was_;
};

}  // namespace

TEST_CASE("isHardwareMidiPort: the names real systems report, and the ones that are not devices") {
    // The one predicate every list and every open set goes through, held against the name shapes the three systems
    // actually report: ALSA's "client:port" suffixes with their moving numbers, WinMM's "N- " index prefix, CoreMIDI's
    // device-qualified names, and our own names as both systems spell them - the stream's "RetroPlug In" and the
    // Launchpad's "RetroPlug Launchpad:<device>" link ports, which a second instance claims and no picker may offer.
    const std::vector<std::string> hardware = {
        "LPProMK3 MIDI:1 24:0",
        "Midisport 2x2:1 28:0",
        "2- MIDISPORT 2x2 Port A",
        "Arturia KeyStep 32",
        "IAC Driver Bus 1",
        "Launchpad Pro MK3 LPProMK3 MIDI",
    };
    const std::vector<std::string> notHardware = {
        "RetroPlug In",                                 // the stream's own virtual port
        "RetroPlug Out",                                // ditto
        "RetroPlug:RetroPlug In 128:0",                 // the same, as ALSA spells it
        "RetroPlug Launchpad:LPProMK3 MIDI 131:0",      // the pack's link port, claimed by this instance
        "Midi Through:Midi Through Port-0 14:0",        // ALSA's loopback, no device behind it
        "Midi Through Port-0",
    };
    for (const std::string& name : hardware) CHECK(isHardwareMidiPort(name, kClient));
    for (const std::string& name : notHardware) CHECK_FALSE(isHardwareMidiPort(name, kClient));
    // Case-sensitive, as the names are: a client name is matched exactly, so a port that merely resembles ours is a
    // device (and a filter would keep it).
    CHECK(isHardwareMidiPort("retroplug in", kClient));
    // The same rows through the index helper, so the table is held where the callers enter.
    std::vector<std::string> all = hardware;
    all.insert(all.end(), notHardware.begin(), notHardware.end());
    CHECK(hardwarePortIndices(all, kClient).size() == hardware.size());
}

TEST_CASE("the allow list matches substrings of a port's name, and an empty one allows everything") {
    // A real backend's name carries the client and port numbers ("...:1 24:0"), which move as clients come and go, so
    // the list is of substrings; tools/run-sdl-controller.sh writes its two fakes' WHOLE names, which no other device's
    // name can contain, so nothing else can then be listed or opened. Unset means every port, so an ordinary run is
    // unchanged.
    REQUIRE(midiPortAllowedBy("LPProMK3 MIDI:1 24:0", "LPProMK3 MIDI,RPTestKeys"));
    // the space around an entry is trimmed
    REQUIRE(midiPortAllowedBy("RPTestKeys:1 32:0", "LPProMK3 MIDI, RPTestKeys"));
    REQUIRE_FALSE(midiPortAllowedBy("Zoom L6:1 20:0", "LPProMK3 MIDI,RPTestKeys"));
    REQUIRE(midiPortAllowedBy("anything at all", ""));       // unset
    REQUIRE(midiPortAllowedBy("anything at all", " , \t"));  // a list of separators is not "no ports at all"
    REQUIRE_FALSE(midiPortAllowedBy("Zoom L6", "zoom l6"));  // case-sensitive, deliberately: names are exact
}

TEST_CASE("a filtered process neither lists nor opens a port outside the list") {
    // The filter rides hardwarePortIndices, which every selection path goes through: the pickers' lists, the inputs'
    // open set, and the output's match. So ONE rule hides a port everywhere, and a filtered stream cannot open what its
    // own Settings list would not show.
    PortAllowList allow("Launchpad MK2");
    REQUIRE(midiPortAllowed("Launchpad MK2"));
    REQUIRE_FALSE(midiPortAllowed("Arturia KeyStep 32"));
    REQUIRE(hardwarePortIndices(kPorts, kClient) == std::vector<std::size_t>{2});  // the Launchpad alone kept
    // "All devices" is every hardware port the filter allows, not every port the machine has.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {}, true) == std::vector<std::size_t>{2});
    // A named pick outside the list resolves to nothing, so nothing opens for it.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Arturia KeyStep 32").has_value());
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Arturia KeyStep 32"}, false).empty());
}

TEST_CASE("a scan's held edge is a Windows-only lever, and a stream that takes it opens nothing") {
    // rp_device_host::suspend_hardware_inputs is asked on every platform, and the SDL answers it by
    // scanSuspendsInputs() (sdl/main.cpp's onSuspendInputs): a Windows input is exclusive, so a scan needs the stream
    // to let go; ALSA's sequencer and CoreMIDI share an input, so the scan hears it anyway and closing the stream's
    // inputs would cost the user their MIDI and an audio restart for nothing. The answer is held here, on whichever
    // platform builds this; that the SDL ACTS on it (it suspends under _WIN32 and logs nothing off it) is held in place
    // by `pnpm sdl:controller`, whose scan leg fails on a "hardware inputs suspended" line.
#if defined(_WIN32)
    REQUIRE(rp::midi::scanSuspendsInputs());
#else
    REQUIRE_FALSE(rp::midi::scanSuspendsInputs());
#endif
    // What the lever does where it acts: a suspended stream opens NOTHING, whichever selection asked - the same pure
    // helper MidiStream::applyInputs goes through, so this is the behaviour and not a restatement of the guard.
    REQUIRE(inputPortsToOpen(kPorts, kClient, {}, true, "", /*suspended=*/true).empty());
    REQUIRE(inputPortsToOpen(kPorts, kClient, {"Launchpad MK2"}, false, "", true).empty());
    // and unsuspended it opens
    REQUIRE_FALSE(inputPortsToOpen(kPorts, kClient, {"Launchpad MK2"}, false, "", false).empty());
}
