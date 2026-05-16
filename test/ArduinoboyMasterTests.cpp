#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "system/sameboy/roles/ArduinoboyMaster.hpp"
#include "transport/MidiTypes.hpp"

// Unit tests for the Arduinoboy MI.OUT byte-stream decoder. Protocol is
// taken verbatim from the Arduinoboy firmware
// (Mode_LSDJ_Midiout.ino). These tests assert the decoder's interpretation
// of each documented byte; they do NOT prove end-to-end with the actual
// LSDJ-aboy ROM (see AGENTS.md "Known gotcha: cycling SYNC past position 3"
// for the navigation limitation that blocks that path).

namespace {

std::vector<::MidiEvent> decode(std::initializer_list<std::uint8_t> bytes) {
    ArduinoboyMaster m;
    std::vector<::MidiEvent> out;
    for (std::uint8_t b : bytes) m.feed(b, out);
    return out;
}

} // namespace

TEST_CASE("ArduinoboyMaster: 0x7F → MIDI clock tick (0xF8)", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0x7F, 0x7F, 0x7F });
    REQUIRE(evs.size() == 3);
    for (const auto& e : evs) {
        REQUIRE(e.size == 1);
        REQUIRE(e.data[0] == 0xF8);
    }
}

TEST_CASE("ArduinoboyMaster: 0x7D → transport start (0xFA), 0x7E → stop (0xFC)", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0x7D, 0x7F, 0x7F, 0x7E });
    REQUIRE(evs.size() == 4);
    REQUIRE(evs[0].data[0] == 0xFA);
    REQUIRE(evs[1].data[0] == 0xF8);
    REQUIRE(evs[2].data[0] == 0xF8);
    REQUIRE(evs[3].data[0] == 0xFC);
}

TEST_CASE("ArduinoboyMaster: 0x70..0x73 + value → NoteOn ch 0..3", "[ArduinoboyMaster]") {
    // Channel 0, note 60 (middle C)
    {
        const auto evs = decode({ 0x70, 60 });
        REQUIRE(evs.size() == 1);
        REQUIRE(evs[0].size == 3);
        REQUIRE(evs[0].data[0] == 0x90); // NoteOn ch 0
        REQUIRE(evs[0].data[1] == 60);
        REQUIRE(evs[0].data[2] == 0x7F);
    }
    // Channel 3, note 72
    {
        const auto evs = decode({ 0x73, 72 });
        REQUIRE(evs.size() == 1);
        REQUIRE(evs[0].data[0] == 0x93); // NoteOn ch 3
        REQUIRE(evs[0].data[1] == 72);
    }
}

TEST_CASE("ArduinoboyMaster: 0x7X note command with value 0 → NoteOff", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0x71, 0 });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].size == 3);
    REQUIRE(evs[0].data[0] == 0x81); // NoteOff ch 1
    REQUIRE(evs[0].data[1] == 0);
    REQUIRE(evs[0].data[2] == 0);
}

TEST_CASE("ArduinoboyMaster: 0x74..0x77 + value → Control Change", "[ArduinoboyMaster]") {
    // Channel 0 (m=4), value 64
    const auto evs = decode({ 0x74, 64 });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].size == 3);
    REQUIRE(evs[0].data[0] == 0xB0); // CC ch 0
    REQUIRE(evs[0].data[1] == 4);    // CC# = m (simplified mapping)
    REQUIRE(evs[0].data[2] == 64);
}

TEST_CASE("ArduinoboyMaster: 0x78..0x7B + value → Program Change", "[ArduinoboyMaster]") {
    // Channel 2 (m=10), patch 7
    const auto evs = decode({ 0x7A, 7 });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].size == 2);
    REQUIRE(evs[0].data[0] == 0xC2); // PC ch 2
    REQUIRE(evs[0].data[1] == 7);
}

TEST_CASE("ArduinoboyMaster: realtime bytes interleave with command/value pairs", "[ArduinoboyMaster]") {
    // Start, clock, then ch1 NoteOn(60), then clock, stop.
    const auto evs = decode({ 0x7D, 0x7F, 0x71, 60, 0x7F, 0x7E });
    REQUIRE(evs.size() == 5);
    REQUIRE(evs[0].data[0] == 0xFA); // start
    REQUIRE(evs[1].data[0] == 0xF8); // clock
    REQUIRE(evs[2].data[0] == 0x91); // NoteOn ch 1
    REQUIRE(evs[2].data[1] == 60);
    REQUIRE(evs[3].data[0] == 0xF8); // clock
    REQUIRE(evs[4].data[0] == 0xFC); // stop
}

TEST_CASE("ArduinoboyMaster: value bytes without a pending command are dropped", "[ArduinoboyMaster]") {
    // 0x40 is a valid value byte but no command precedes it — should be
    // silently dropped (and only the realtime that follows decoded).
    const auto evs = decode({ 0x40, 0x7F });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].data[0] == 0xF8);
}

TEST_CASE("ArduinoboyMaster: high-bit bytes (>=0x80) are ignored", "[ArduinoboyMaster]") {
    // KEYBD-mode polling bytes look like 0x55/0xAA/0xFF — these aren't
    // MI.OUT protocol bytes. The decoder must ignore them so spurious
    // captures during mode transitions don't pollute the MIDI log.
    const auto evs = decode({ 0xFF, 0xAA, 0x55, 0xD5, 0x7F });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].data[0] == 0xF8);
}

TEST_CASE("ArduinoboyMaster::reset clears pending state mid-command", "[ArduinoboyMaster]") {
    ArduinoboyMaster m;
    std::vector<::MidiEvent> out;
    m.feed(0x71, out);  // pending NoteOn ch 1
    REQUIRE(out.empty());
    m.reset();
    m.feed(60, out);    // would have completed the note; now dropped
    REQUIRE(out.empty());
    m.feed(0x7F, out);  // realtime still works
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].data[0] == 0xF8);
}

TEST_CASE("ArduinoboyMaster: command commands >= 0x7C aren't real commands but realtime falls in 0x7D-0x7F", "[ArduinoboyMaster]") {
    // 0x7C is the last "needs a value" command id (m = 0x0C - which is the
    // edge of the PC range; the firmware treats m >= 0x0C as undefined and
    // drops the action). Verify our decoder consumes the value byte but
    // emits nothing.
    const auto evs = decode({ 0x7C, 42, 0x7F });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].data[0] == 0xF8); // only the clock survived
}
