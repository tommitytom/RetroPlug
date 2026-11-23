#include "host/packs/ProcessPacks.hpp"

#include <cstdio>
#include <mutex>

#include "graph/Packages.hpp"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackDiscovery.hpp"

namespace rp::host::packs {

PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    // The data root is the user's, so a development folder inside it is held like an installed one.
    static PackTable table(rp::graph::productPackages(), defaultPackRoots().data);
    return table;
}

void ensureLaunchPacks() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (const PackRefusal& refusal : loadLaunchPacks(processPacks(), defaultPackRoots(), {}))
            std::fprintf(stderr, "[packs] %s\n", refusal.reason.c_str());
    });
}

}  // namespace rp::host::packs
