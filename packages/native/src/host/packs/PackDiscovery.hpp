#pragma once

#include <filesystem>
#include <vector>

#include "host/packs/PackCandidate.hpp"
#include "host/packs/PackDirs.hpp"

// Which packs a launch loads (docs/downloadable-packs-loading-design.md section 1): each installed pack's active
// version, from state.json, and every development pack folder, RETROPLUG_PACK_PATH's and the ones a host passes (the
// CLI's --pack and Settings' Development packs, M4). A development pack overrides an installed one with the same id.
namespace rp::host::packs {

class PackTable;

struct PackDiscovery {
    std::vector<PackCandidate> candidates;  // installed in state.json's order, then development in the order given
    std::vector<PackRefusal>   refused;     // a folder that could not be a candidate, with the reason
};

// The candidates under `roots` and in `devDirs` (in order; a later folder of an id already given is refused). A
// state.json that does not read (corrupt, or a newer RetroPlug's) loads no installed pack and is reported once.
PackDiscovery discoverPacks(const PackRoots& roots, const std::vector<std::filesystem::path>& devDirs);

// What a host's launch does. Under one exclusive hold of state.lock: settle state.json (pending updates and removals),
// read it, discover, and take the hold on each installed candidate's version folder; then, with state.lock let go,
// load in dependency order into `table`, which keeps those holds. So another launch's settle, which takes state.lock
// too, deletes a version folder either before this launch discovers it or not at all. The development folders are
// RETROPLUG_PACK_PATH's followed by `extraDevDirs`. Returns every refusal, discovery's and the loads'.
std::vector<PackRefusal> loadLaunchPacks(PackTable& table, const PackRoots& roots,
                                         const std::vector<std::filesystem::path>& extraDevDirs);

}  // namespace rp::host::packs
