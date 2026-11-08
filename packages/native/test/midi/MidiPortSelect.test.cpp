// Unit-tests the pure, RtMidi-free port-selection helpers in MidiIo.hpp (hardwarePortIndices / matchPortIndex /
// inputPortsToOpen). These decide which hardware ports MidiIo opens for a given device selection - the whole
// Settings > MIDI policy, including the two things that make a port NOT open: the input default is None
// (opening every device is now an explicit choice), and a port a control surface has claimed is skipped. No
// MIDI system or RtMidi runtime is needed: the helpers operate on a plain vector<string> of port names.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include "host/input/MidiIo.hpp"
#include "launchpad/desktop/MidiPortFilter.hpp"  // the pack's copy of the one allow-list rule (the shell's, below)

using retroplug::hardwarePortIndices;
using retroplug::matchPortIndex;
using retroplug::inputPortsToOpen;
using retroplug::midiPortAllowed;
using retroplug::midiPortAllowedBy;

namespace {
/** RP_MIDI_PORT_ALLOW is a developer's environment variable (tools/run-sdl-controller.sh exports it for the standalone's
 *  harness), and a hand run of that harness leaves it in the shell. Every case here but the filter's own assumes the
 *  UNFILTERED answer, so the file clears it on load and puts back whatever it found at exit; a case that wants a list
 *  sets it for its own scope (PortAllowList). */
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
    "RetroPlug In",       // 0: our own virtual port (contains the client name) — always skipped
    "Midi Through Port-0", // 1: ALSA MIDI-through — always skipped
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

// The open-list policy MidiIo applies, expressed via the helpers: a specific input = just that one (or none
// when absent); output is the same.
TEST_CASE("selection policy: input opens all-vs-one, output opens one-or-none") {
    // Input/output, a specific present device → exactly that port.
    REQUIRE(matchPortIndex(kPorts, kClient, "Launchpad MK2").value() == 2);
    // A specific absent device → nothing opened.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Ghost").has_value());
}

// The INPUT default is None, not every device. Opening everything is a surprising amount of behaviour to get
// without asking for it: anything plugged in becomes a MIDI source, so a control surface's free-running clock
// drives the host tempo and a controller's mixer ports send notes at the cart. The virtual "<client> In" port
// is always open regardless, so this only makes PHYSICAL devices opt-in.
TEST_CASE("inputPortsToOpen: the default selection opens no hardware at all") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, "").empty());
}

TEST_CASE("inputPortsToOpen: the explicit all-devices sentinel opens every hardware port") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, retroplug::kAllInputs) == std::vector<std::size_t>{2, 3});
}

TEST_CASE("inputPortsToOpen: a named device opens exactly that port, and an absent one opens nothing") {
    REQUIRE(inputPortsToOpen(kPorts, kClient, "Arturia KeyStep 32") == std::vector<std::size_t>{3});
    REQUIRE(inputPortsToOpen(kPorts, kClient, "Some Unplugged Synth").empty());
}

TEST_CASE("inputPortsToOpen: a reserved port is skipped whichever way it is selected") {
    // Both paths matter: "All Devices" is the case that would merge a Launchpad's pad presses into the
    // musical stream, and naming it explicitly must not smuggle it back in.
    REQUIRE(inputPortsToOpen(kPorts, kClient, retroplug::kAllInputs, "Launchpad MK2") == std::vector<std::size_t>{3});
    REQUIRE(inputPortsToOpen(kPorts, kClient, "Launchpad MK2", "Launchpad MK2").empty());
}

// A port a control surface has claimed exclusively is skipped by BOTH helpers. Not tidiness: a pad press is a
// NoteOn, and LSDj's MI.MAP translator reads a NoteOn as a row launch, so a Launchpad sharing the musical
// stream would fire every launch twice - once quantised by the controller app, once raw. "All Devices" is the
// case that would otherwise do it.
TEST_CASE("a reserved port drops out of All Devices") {
    const auto open = hardwarePortIndices(kPorts, kClient, "Launchpad MK2");
    REQUIRE(open == std::vector<std::size_t>{3});  // the KeyStep still plays; the surface does not
}

TEST_CASE("a reserved port cannot be selected as the input device either") {
    // Selecting it explicitly must not smuggle it back in - the link owns the OS port, so opening it twice
    // would fail anyway, and silently half-working is worse than not opening it.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Launchpad MK2", "Launchpad MK2").has_value());
    REQUIRE(matchPortIndex(kPorts, kClient, "Arturia KeyStep 32", "Launchpad MK2").value() == 3);
}

TEST_CASE("no reservation is the default and changes nothing") {
    REQUIRE(hardwarePortIndices(kPorts, kClient, "") == hardwarePortIndices(kPorts, kClient));
    // A reserved name that is not present is simply inert (the device was unplugged, the link is down).
    REQUIRE(hardwarePortIndices(kPorts, kClient, "Some Unplugged Launchpad").size() == 2);
}

// --- System Real-Time extraction -------------------------------------------------------------------
// Clock/start/stop are a stream within the stream: the spec allows them at ANY byte boundary, and a
// transport may hand several events over in one buffer. The drain used to recognise them only as a
// one-byte message, so a batched pair counted as no clock at all - which reads downstream as a tempo a
// whole ratio slow, and puts a stray 0xF8 into whatever message it was riding along with.

static std::vector<std::uint8_t> realtimeOf(std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint8_t> seen;
    retroplug::extractRealtime(bytes, [&](std::uint8_t b) { seen.push_back(b); });
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

TEST_CASE("the allow list matches substrings of a port's name, and an empty one allows everything") {
    // A real backend's name carries the client and port numbers ("...:1 24:0"), which move as clients come and go, so the
    // list is of substrings; tools/run-sdl-controller.sh writes its two fakes' WHOLE names, which no other device's name can
    // contain, so nothing else can then be listed or opened. Unset means every port, so an ordinary run is unchanged.
    REQUIRE(midiPortAllowedBy("LPProMK3 MIDI:1 24:0", "LPProMK3 MIDI,RPTestKeys"));
    REQUIRE(midiPortAllowedBy("RPTestKeys:1 32:0", "LPProMK3 MIDI, RPTestKeys"));  // the space around an entry is trimmed
    REQUIRE_FALSE(midiPortAllowedBy("Zoom L6:1 20:0", "LPProMK3 MIDI,RPTestKeys"));
    REQUIRE(midiPortAllowedBy("anything at all", ""));        // unset
    REQUIRE(midiPortAllowedBy("anything at all", " , \t"));   // a list of separators is not "no ports at all"
    REQUIRE_FALSE(midiPortAllowedBy("Zoom L6", "zoom l6"));   // case-sensitive, deliberately: names are exact
}

TEST_CASE("the shell's rule and the pack's agree on every input, which is what keeps two copies one rule") {
    // MidiIo.hpp and the Launchpad pack's MidiPortFilter.hpp hold the same rule twice, because a package may include its
    // own tree and its declared packages' and not the shell's. It is ONE rule, though: both bodies are run over the same
    // table here, so an edit to either alone fails this case rather than drifting quietly. The names are the ones the real
    // callers see - a whole ALSA name, a Windows one, an entry with a trailing space, a list of separators.
    struct Row {
        const char* name;
        const char* spec;
    };
    const Row rows[] = {
        {"RPFakeLPProMK3:LPProMK3 MIDI 128:0", "RPFakeLPProMK3:LPProMK3 MIDI 128:0,RPTestKeys:RPTestKeys 129:0"},
        {"RPTestKeys:RPTestKeys 129:0", "RPFakeLPProMK3:LPProMK3 MIDI 128:0, RPTestKeys:RPTestKeys 129:0"},
        {"LPProMK3 MIDI:LPProMK3 MIDI 128:0", "RPFakeLPProMK3:LPProMK3 MIDI 128:0"},  // the client name is the entry
        {"Launchpad Pro MK3:LPProMK3 MIDI 24:0", "RPFakeLPProMK3"},                   // a real device, refused
        {"Zoom L6:1 20:0", "LPProMK3 MIDI,RPTestKeys"},
        {"anything at all", ""},
        {"anything at all", " , \t"},
        {"Launchpad MK2", "Launchpad MK2"},
        {"launchpad mk2", "Launchpad MK2"},
        {"MIDIIN2 (LPProMK3 MIDI) 2", ""},
    };
    for (const Row& r : rows) {
        INFO(r.name << " under '" << r.spec << "'");
        REQUIRE(retroplug::midiPortAllowedBy(r.name, r.spec) == rp::launchpad::midiPortAllowedBy(r.name, r.spec));
    }
}

TEST_CASE("a filtered process neither lists nor opens a port outside the list") {
    // The filter rides hardwarePortIndices, which every selection path goes through: the pickers' lists
    // (hardwarePortNames), the inputs' open set (inputPortsToOpen), and the output's (matchPortIndex). So ONE rule hides a
    // port everywhere, and a filtered stream cannot open what its own Settings list would not show.
    PortAllowList allow("Launchpad MK2");
    REQUIRE(midiPortAllowed("Launchpad MK2"));
    REQUIRE_FALSE(midiPortAllowed("Arturia KeyStep 32"));
    REQUIRE(hardwarePortIndices(kPorts, kClient) == std::vector<std::size_t>{2});  // the Launchpad alone kept
    REQUIRE(hardwarePortIndices(kPorts, kClient).size() == 1);
    // "All Devices" is every hardware port the filter allows, not every port the machine has.
    REQUIRE(inputPortsToOpen(kPorts, kClient, retroplug::kAllInputs) == std::vector<std::size_t>{2});
    // A named pick outside the list resolves to nothing, so nothing opens for it.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Arturia KeyStep 32").has_value());
    REQUIRE(inputPortsToOpen(kPorts, kClient, "Arturia KeyStep 32").empty());
}

TEST_CASE("a scan's held edge is a Windows-only lever, and a stream that takes it opens nothing") {
    // rp_device_host::suspend_hardware_inputs is asked on every platform, and the SDL answers it by scanSuspendsInputs()
    // (sdl/main.cpp's onSuspendInputs): a Windows input is exclusive, so a scan needs the stream to let go; ALSA's
    // sequencer and CoreMIDI share an input, so the scan hears it anyway and closing the stream's inputs would cost the
    // user their MIDI and an audio restart for nothing. The answer is held here, on whichever platform builds this; that
    // the SDL ACTS on it (it suspends under _WIN32 and logs nothing off it) is held in place by `pnpm sdl:controller`,
    // whose scan leg fails on a "hardware inputs suspended" line.
#if defined(_WIN32)
    REQUIRE(retroplug::scanSuspendsInputs());
#else
    REQUIRE_FALSE(retroplug::scanSuspendsInputs());
#endif
    // What the lever does where it acts: a suspended stream opens NOTHING, whichever selection asked - the same pure
    // helper MidiIo::inputsFor goes through, so this is the behaviour and not a restatement of the guard.
    REQUIRE(inputPortsToOpen(kPorts, kClient, retroplug::kAllInputs, "", /*suspended=*/true).empty());
    REQUIRE(inputPortsToOpen(kPorts, kClient, "Launchpad MK2", "", true).empty());
    REQUIRE_FALSE(inputPortsToOpen(kPorts, kClient, "Launchpad MK2", "", false).empty());  // and unsuspended it opens
}
