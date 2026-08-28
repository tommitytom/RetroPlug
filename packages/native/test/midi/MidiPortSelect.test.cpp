// Unit-tests the pure, RtMidi-free port-selection helpers in MidiIo.hpp (hardwarePortIndices / matchPortIndex /
// inputPortsToOpen). These decide which hardware ports MidiIo opens for a given device selection - the whole
// Settings > MIDI policy, including the two things that make a port NOT open: the input default is None
// (opening every device is now an explicit choice), and a port a control surface has claimed is skipped. No
// MIDI system or RtMidi runtime is needed: the helpers operate on a plain vector<string> of port names.

#include <catch2/catch_test_macros.hpp>

#include "host/input/MidiIo.hpp"

using retroplug::hardwarePortIndices;
using retroplug::matchPortIndex;
using retroplug::inputPortsToOpen;

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
