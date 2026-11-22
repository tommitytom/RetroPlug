#include "host/packs/ProcessPacks.hpp"

#include "graph/Packages.hpp"

namespace rp::host::packs {

PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    static PackTable table(rp::graph::productPackages());
    return table;
}

}  // namespace rp::host::packs
