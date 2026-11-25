#include "host/packs/ProcessPacks.hpp"

#include <cstdio>
#include <exception>
#include <mutex>

#include "graph/Packages.hpp"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackDiscovery.hpp"
#include "host/packs/KindsTrial.hpp"
#include "host/packs/ProtocolTrial.hpp"

namespace rp::host::packs {

PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    // The data root is the user's, so a development folder inside it is held like an installed one. The protocol and
    // kinds trials are the backend's, which this file is in and the table's library cannot reach.
    static PackTable table(rp::graph::productPackages(), defaultPackRoots().data);
    static const bool trialSet = (table.setProtocolTrial(protocolTrial()), table.setKindsTrial(kindsTrial()), true);
    (void)trialSet;
    return table;
}

void ensureLaunchPacks() {
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            for (const PackRefusal& refusal : loadLaunchPacks(processPacks(), defaultPackRoots(), {}))
                std::fprintf(stderr, "[packs] %s\n", refusal.reason.c_str());
        } catch (const std::exception& e) {
            // Reported once and never thrown: this runs inside a plugin's constructor.
            std::fprintf(stderr, "[packs] the launch load failed: %s\n", e.what());
        }
    });
}

}  // namespace rp::host::packs
