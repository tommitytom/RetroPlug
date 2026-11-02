#pragma once

// RETROPLUG_DETERMINISTIC=1 powers every core on in a fixed state instead of a random one: SameBoy's
// GB_random() returns 0 (it otherwise seeds from time(NULL), filling WRAM / HRAM / OAM / wave RAM at
// reset), Mesen NES RAM powers on as all zeros (NesConfig defaults to Random), and the SMS/GG VDP starts
// at scanline 0 (SmsConfig::RandomizeVdpStartScanline, a field RetroPlug adds to the vendored Mesen).
// Real hardware powers on unpredictably, which is what the app keeps. The test-native runner sets this for the
// parity suite (test-native/parity) and for render-baseline, render-chunks and render-cli (its DETERMINISTIC_FILES),
// and so for every engine job's Engine in those processes, so a recorded hash cannot depend on the wall clock; the C++
// engine-graph binary sets it for itself (test/engine-graph/Deterministic.cpp); and the Node host's parity test
// (packages/native/node/test/emu-parity.test.mjs) sets it for both hosts it compares, which are two processes that
// would otherwise each draw a power-on state of their own. Carts with a real-time clock (MBC3
// timer, HuC3, TPP1) still read the host clock in SameBoy's default RTC mode, and no parity or render case uses one.

#include <cstdlib>
#include <cstring>

namespace rp {

// Read once per process: the SameBoy flag it drives is a process-global static anyway.
inline bool deterministic() {
    static const bool on = [] {
        const char* v = std::getenv("RETROPLUG_DETERMINISTIC");
        return v != nullptr && std::strcmp(v, "1") == 0;
    }();
    return on;
}

} // namespace rp
