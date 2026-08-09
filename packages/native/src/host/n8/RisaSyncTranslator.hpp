#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace retroplug {

// Pure, I/O-free translator: incoming MIDI real-time / Song-Position bytes -> risa's host-sync byte
// protocol (arm+locate / start / 24-PPQN clock / stop), for streaming to a physical Everdrive N8 running
// the risa NES tracker. The C++ twin of the TS `risa-sync` DSP role (packages/retroplug/src/dspRoles.ts)
// + packages/retroplug/src/risaSync.ts, but driven by an EXTERNAL MIDI clock master (a DAW / hardware
// sequencer) instead of a DAW block transport. Keep this in step with risaSync.ts; the pure-TS test
// packages/retroplug/test/dsp/risa-sync.test.ts is the golden reference for the emitted bytes.
//
// Wire format (risaSync.ts): NOT MIDI - a raw byte stream reusing MIDI status VALUES. 0xF8/0xFA/0xFC are
// real System Real-Time (clock/start/stop); 0xF9 0x52 ss cc tt is risa's private 5-byte arm+locate packet.
//
// Locate policy (honor Song Position when sent, else from the top):
//   - Start (0xFA): arm from the TOP (song start). MIDI Start is defined as "from position 0".
//   - Continue (0xFB): arm from the current position - the last Song Position Pointer (0xF2), or where a
//     prior Stop left the playhead. This is how a host locates mid-song: SPP then Continue.
//   - Song Position Pointer (0xF2 lsb msb): set the position a subsequent Continue arms from.
// Mid-song locate is only exact for a uniformly-laid-out song (docs/risa-host-sync-report.md); playing from
// the top is always correct.
class RisaSyncTranslator {
public:
    // Protocol constants (mirror risaSync.ts). Public so tests can assert against them.
    static constexpr std::uint8_t RISA_LOCATE_STATUS   = 0xF9;  // arm+locate packet status
    static constexpr std::uint8_t RISA_LOCATE_SUB      = 0x52;  // arm sub-command
    static constexpr std::uint8_t RISA_START           = 0xFA;  // transport start (plays the armed locate)
    static constexpr std::uint8_t RISA_CLOCK           = 0xF8;  // one 24-PPQN sequencer tick
    static constexpr std::uint8_t RISA_STOP            = 0xFC;  // transport stop
    static constexpr std::int64_t RISA_PPQN            = 24;    // clocks per quarter note
    static constexpr std::int64_t RISA_CLOCKS_PER_PHRASE = 96;  // 16 rows * six-clock locate grid

    // MIDI status bytes this translator acts on (everything else - notes/CC/sysex - is ignored: risa is
    // transport-driven, not note-driven).
    static constexpr std::uint8_t MIDI_SPP      = 0xF2;  // Song Position Pointer: F2 lsb msb (14-bit, 16ths)
    static constexpr std::uint8_t MIDI_CLOCK    = 0xF8;
    static constexpr std::uint8_t MIDI_START    = 0xFA;
    static constexpr std::uint8_t MIDI_CONTINUE = 0xFB;
    static constexpr std::uint8_t MIDI_STOP     = 0xFC;

    // The 5-byte arm+locate packet for an absolute 24-PPQN clock (== risaSync.ts risaLocate + risaArmPacket):
    //   phrase = clock / 96;  songRow = (phrase >> 4) & 0x7f;  chainRow = phrase & 0x0f;  tick = clock % 96
    // Static + pure so a test can assert it against the golden risaSync.ts values directly.
    static std::array<std::uint8_t, 5> armPacket(std::int64_t absoluteClock);

    // Feed one inbound MIDI message exactly as MidiIo delivers it (raw bytes, one message). Appends any risa
    // output bytes to `out` (the caller writes them to the cart FIFO). `out` is NOT cleared first - the
    // caller owns it. Returns nothing; introspect via the accessors below.
    void onMessage(const std::uint8_t* bytes, std::size_t n, std::vector<std::uint8_t>& out);

    // Test / status introspection.
    bool         playing() const { return playing_; }
    std::int64_t absoluteClock() const { return absoluteClock_; }

private:
    void arm(std::vector<std::uint8_t>& out);  // emit armPacket(absoluteClock_) then RISA_START

    std::int64_t absoluteClock_    = 0;      // 24-PPQN clock since song start; set by SPP, advanced per F8
    bool         playing_          = false;
    bool         suppressNextClock_ = false;  // risa primes the armed clock itself - skip one F8 after a start
};

}  // namespace retroplug
