#pragma once

// RETROPLUG_DETERMINISTIC=1 powers every core on in a fixed state instead of a random one: SameBoy's
// GB_random() returns 0 (it otherwise seeds from time(NULL), filling WRAM / HRAM / OAM / wave RAM at
// reset), and Mesen NES RAM powers on as all zeros (NesConfig defaults to Random). Real hardware powers
// on unpredictably, which is what the app keeps; only the parity suite (test-native/parity) sets this, so
// a recorded hash cannot depend on the wall clock. Carts with a real-time clock (MBC3 timer, HuC3, TPP1)
// still read the host clock in SameBoy's default RTC mode, and no parity case uses one.

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
