// Unit-tests the pure, RtMidi-free port-selection helpers in MidiIo.hpp (hardwarePortIndices / matchPortIndex).
// These decide which hardware ports MidiIo opens for a given device selection — the core of the Settings > MIDI
// behavior change (auto-open-every-input → open the SELECTED input; open the SELECTED output). No MIDI system
// or RtMidi runtime is needed: the helpers operate on a plain vector<string> of port names.

#include <catch2/catch_test_macros.hpp>

#include "host/input/MidiIo.hpp"

using retroplug::hardwarePortIndices;
using retroplug::matchPortIndex;

namespace {
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

// The open-list policy MidiIo applies, expressed via the helpers: empty input selection = every hardware input;
// a specific input = just that one (or none when absent); output is the same minus the "all" default.
TEST_CASE("selection policy: input opens all-vs-one, output opens one-or-none") {
    // Input, All Devices (empty) → every hardware port.
    REQUIRE(hardwarePortIndices(kPorts, kClient).size() == 2);
    // Input/output, a specific present device → exactly that port.
    REQUIRE(matchPortIndex(kPorts, kClient, "Launchpad MK2").value() == 2);
    // A specific absent device → nothing opened.
    REQUIRE_FALSE(matchPortIndex(kPorts, kClient, "Ghost").has_value());
}

// A port a control surface has claimed exclusively is skipped by BOTH helpers. Not tidiness: a pad press is a
// NoteOn, and LSDj's MI.MAP translator reads a NoteOn as a row launch, so a Launchpad sharing the musical
// stream would fire every launch twice - once quantised by the controller app, once raw. "All Devices" is the
// case that would otherwise do it, and it is the default.
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
