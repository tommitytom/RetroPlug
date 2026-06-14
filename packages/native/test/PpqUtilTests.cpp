#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "system/SystemTypes.hpp"
#include "util/PpqUtil.hpp"

// PpqUtil::eachTick generates the MIDI clock ticks (24 PPQN) the LsdjSyncRole
// feeds LSDj. A Reaper render showed LSDj's MidiSync playback drifts ~one tick
// (20.8 ms) at a few discrete points over an hour — i.e. clock ticks are
// occasionally dropped. These tests reproduce that WITHOUT Reaper or the
// emulator, driving eachTick block-by-block exactly as PluginDSP::run does
// (ppq computed fresh from the absolute frame position each block) and checking
// the emitted tick indices form a contiguous 0,1,2,… sequence — every gap is a
// dropped tick (LSDj falls behind), every repeat a doubled tick (LSDj races).

namespace {

struct TickAudit {
    std::int64_t fired = 0;
    std::int64_t drops = 0;   // missing tick indices (LSDj behind)
    std::int64_t dups  = 0;   // repeated tick indices (LSDj ahead)
    std::int64_t firstBad = -1;
    std::int64_t lastFired = -1;
    bool first = true;

    void see(std::uint32_t tick) {
        const std::int64_t t = static_cast<std::int64_t>(tick);
        ++fired;
        if (!first) {
            if (t == lastFired) { if (++dups == 1 && firstBad < 0) firstBad = t; }
            else if (t > lastFired + 1) {
                if (firstBad < 0) firstBad = lastFired + 1;
                drops += t - lastFired - 1;
            }
        }
        lastFired = t;
        first = false;
    }
};

// Drive eachTick over `seconds` of audio at the given block size / tempo,
// computing ppq the way PluginDSP::run does for a host without BBT:
//   ppq = (framePos / sampleRate) * (bpm / 60)
TickAudit auditTicks(double seconds, std::uint32_t blockFrames, double bpm,
                     double sr = 44100.0, std::uint32_t resolution = 24) {
    TickAudit a;
    std::int64_t nextTick = 0; // persistent cursor, exactly as LsdjSyncRole holds it
    const std::uint64_t totalFrames =
        static_cast<std::uint64_t>(seconds * sr + 0.5);
    for (std::uint64_t pos = 0; pos < totalFrames; pos += blockFrames) {
        const std::uint32_t frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(blockFrames, totalFrames - pos));
        const double ppq = (static_cast<double>(pos) / sr) * (bpm / 60.0);
        AudioBlockInfo info{ frames, sr, bpm, ppq, /*transportPlaying*/ true };
        PpqUtil::eachTick(info, resolution, nextTick, [&](std::uint32_t tick, std::uint32_t) {
            a.see(tick);
        });
    }
    return a;
}

} // namespace

TEST_CASE("MidiSync clock ticks are contiguous over an hour (no drift)", "[ppq]") {
    // 1 hour at 120 BPM, 1024-frame blocks (the Reaper render's block size).
    const TickAudit a = auditTicks(/*seconds*/ 3600.0, /*block*/ 1024, /*bpm*/ 120.0);

    // 120 BPM = 2 beats/s * 24 ticks = 48 ticks/s -> ~172800 over an hour.
    INFO("fired=" << a.fired << " drops=" << a.drops << " dups=" << a.dups
         << " firstBadTick=" << a.firstBad
         << " (~beat " << (a.firstBad >= 0 ? a.firstBad / 24 : -1) << ")");
    CHECK(a.drops == 0);   // no dropped MIDI clock ticks (LSDj never falls behind)
    CHECK(a.dups == 0);    // no doubled ticks (LSDj never races ahead)
}

TEST_CASE("MidiSync clock tick count is exact over an hour", "[ppq]") {
    const TickAudit a = auditTicks(3600.0, 1024, 120.0);
    // Ticks span indices 0..N inclusive; an exact run emits (last - first + 1).
    const std::int64_t span = a.lastFired + 1; // first tick index is 0
    CHECK(a.fired == span); // fired == span iff no gaps and no repeats
}
