#include "host/packs/ProcessPacks.hpp"

#include "graph/Packages.hpp"
#include "host/packs/PackDirs.hpp"

namespace rp::host::packs {

PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    // The data root is the user's, so a development folder inside it is held like an installed one.
    static PackTable table(rp::graph::productPackages(), defaultPackRoots().data);
    return table;
}

}  // namespace rp::host::packs
