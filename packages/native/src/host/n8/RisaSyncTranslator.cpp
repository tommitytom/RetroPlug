#include "RisaSyncTranslator.hpp"

namespace retroplug {

std::array<std::uint8_t, 5> RisaSyncTranslator::armPacket(std::int64_t absoluteClock) {
    if (absoluteClock < 0) absoluteClock = 0;
    const std::int64_t phrase   = absoluteClock / RISA_CLOCKS_PER_PHRASE;
    const std::uint8_t songRow  = static_cast<std::uint8_t>((phrase >> 4) & 0x7f);
    const std::uint8_t chainRow = static_cast<std::uint8_t>(phrase & 0x0f);
    const std::uint8_t tick     = static_cast<std::uint8_t>((absoluteClock % RISA_CLOCKS_PER_PHRASE) & 0x7f);
    return { RISA_LOCATE_STATUS, RISA_LOCATE_SUB, songRow, chainRow, tick };
}

void RisaSyncTranslator::arm(std::vector<std::uint8_t>& out) {
    // F9 52 ss cc tt (the arm is a barrier: risa discards the old position's queued clocks), then FA start.
    const std::array<std::uint8_t, 5> pkt = armPacket(absoluteClock_);
    out.insert(out.end(), pkt.begin(), pkt.end());
    out.push_back(RISA_START);
    playing_ = true;
    // risa performs one priming sequencer tick itself when it applies the locate, so the armed clock must
    // NOT also get an F8 (it would double-advance the row). Suppress exactly the next incoming clock.
    suppressNextClock_ = true;
}

void RisaSyncTranslator::onMessage(const std::uint8_t* bytes, std::size_t n, std::vector<std::uint8_t>& out) {
    if (n == 0) return;
    const std::uint8_t status = bytes[0];

    switch (status) {
        case MIDI_SPP:
            // Song Position Pointer: F2 lsb msb, a 14-bit position in MIDI beats (sixteenth notes). Each
            // sixteenth is six 24-PPQN clocks. Sets the position a subsequent Continue arms from.
            if (n >= 3) {
                const std::int64_t pos16th = (static_cast<std::int64_t>(bytes[2] & 0x7f) << 7) |
                                             static_cast<std::int64_t>(bytes[1] & 0x7f);
                absoluteClock_ = pos16th * 6;
            }
            return;

        case MIDI_START:
            // MIDI Start is defined as "from the top" - arm at clock 0 regardless of any prior position.
            absoluteClock_ = 0;
            arm(out);
            return;

        case MIDI_CONTINUE:
            // Continue resumes from the current position (last SPP, or where Stop left the playhead).
            arm(out);
            return;

        case MIDI_STOP:
            // Gate + stop. Keep absoluteClock_ so a following Continue resumes in place.
            out.push_back(RISA_STOP);
            playing_ = false;
            suppressNextClock_ = false;
            return;

        case MIDI_CLOCK:
            if (!playing_) return;  // clocks outside transport are ignored (matches the role's transport gate)
            // Advance the position on every received tick so a later re-arm's locate stays aligned, but
            // suppress emitting the one armed clock that risa primed itself.
            ++absoluteClock_;
            if (suppressNextClock_) { suppressNextClock_ = false; return; }
            out.push_back(RISA_CLOCK);
            return;

        default:
            // Notes / CC / sysex / other real-time (active-sensing, reset): risa is transport-driven, ignore.
            return;
    }
}

}  // namespace retroplug
