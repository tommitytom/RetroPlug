#pragma once

// RETROPLUG_DETERMINISTIC=1 powers every core on in a fixed state instead of a random one: SameBoy's
// GB_random() returns 0 (it otherwise seeds from time(NULL), filling WRAM / HRAM / OAM / wave RAM at
// reset), and Mesen NES RAM powers on as all zeros (NesConfig defaults to Random). Real hardware powers
// on unpredictably, which is what the app keeps; only the parity harness (test-native/parity and
// tools/run-reaper-parity.sh) sets this, so a recorded hash cannot depend on the wall clock.

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
