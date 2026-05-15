#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "system/sameboy/roles/ArduinoboyMaster.hpp"
#include "transport/MidiTypes.hpp"

namespace {

// Convenience: feed a sequence of bytes through the decoder and return the
// flat MidiEvent buffer.
std::vector<::MidiEvent> decode(std::initializer_list<std::uint8_t> bytes) {
    ArduinoboyMaster m;
    std::vector<::MidiEvent> out;
    for (std::uint8_t b : bytes) m.feed(b, out);
    return out;
}

} // namespace

TEST_CASE("ArduinoboyMaster forwards realtime clock bytes verbatim", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0xF8, 0xF8, 0xF8 });
    REQUIRE(evs.size() == 3);
    for (const auto& e : evs) {
        REQUIRE(e.size == 1);
        REQUIRE(e.data[0] == 0xF8);
    }
}

TEST_CASE("ArduinoboyMaster forwards start (0xFA) and stop (0xFC) transport bytes", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0xFA, 0xF8, 0xF8, 0xFC });
    REQUIRE(evs.size() == 4);
    REQUIRE(evs[0].data[0] == 0xFA);
    REQUIRE(evs[1].data[0] == 0xF8);
    REQUIRE(evs[2].data[0] == 0xF8);
    REQUIRE(evs[3].data[0] == 0xFC);
}

TEST_CASE("ArduinoboyMaster decodes a tagged channel note as a two-byte NoteOn", "[ArduinoboyMaster]") {
    // Channel 0 + note 60 → NoteOn ch1, note 60, velocity 0x7F.
    const auto evs = decode({ 0x00, 60 });
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].size == 3);
    REQUIRE(evs[0].data[0] == 0x90); // NoteOn ch 0
    REQUIRE(evs[0].data[1] == 60);
    REQUIRE(evs[0].data[2] == 0x7F);
}

TEST_CASE("ArduinoboyMaster routes per-channel notes to the right MIDI channel", "[ArduinoboyMaster]") {
    const auto evs = decode({ 0x03, 72 });  // channel 3 → MIDI ch 4
    REQUIRE(evs.size() == 1);
    REQUIRE(evs[0].size == 3);
    REQUIRE(evs[0].data[0] == 0x93); // NoteOn channel 3
    REQUIRE(evs[0].data[1] == 72);
}

TEST_CASE("ArduinoboyMaster interleaves clock and note commands", "[ArduinoboyMaster]") {
    // Clock, then note tag (0x01 → ch 1, follow-up byte 64), then more clocks.
    const auto evs = decode({ 0xF8, 0x01, 64, 0xF8, 0xF8 });
    REQUIRE(evs.size() == 4);
    REQUIRE(evs[0].data[0] == 0xF8);

    REQUIRE(evs[1].size == 3);
    REQUIRE(evs[1].data[0] == 0x91);
    REQUIRE(evs[1].data[1] == 64);

    REQUIRE(evs[2].data[0] == 0xF8);
    REQUIRE(evs[3].data[0] == 0xF8);
}

TEST_CASE("ArduinoboyMaster::reset clears any in-flight pending command", "[ArduinoboyMaster]") {
    ArduinoboyMaster m;
    std::vector<::MidiEvent> out;
    m.feed(0x02, out);   // expects a follow-up note byte
    REQUIRE(out.empty());
    m.reset();
    m.feed(0xF8, out);   // realtime clock — must not be misread as the pending note
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].data[0] == 0xF8);
}
