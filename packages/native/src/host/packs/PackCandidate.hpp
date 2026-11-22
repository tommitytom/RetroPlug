#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "host/packs/FileLock.hpp"

// What a launch hands the loader (host/packs/PackDiscovery.hpp) and what the loader answers when it refuses
// (host/packs/PackTable.hpp): a version folder and where it came from, and a refusal with its reason.
namespace rp::host::packs {

enum class PackOrigin { Installed, Development };

// A pack the loader may load: its version folder and where it was found. An installed one found by a launch carries
// the hold on its folder the launch took while it held state.lock (loadLaunchPacks), so no other launch's settle can
// delete the folder between its discovery and its load; the loader takes one itself for a candidate without.
struct PackCandidate {
    std::filesystem::path     dir;
    PackOrigin                origin = PackOrigin::Development;
    bool                      overridesInstalled = false;  // a development pack standing in for an installed one
    std::shared_ptr<FileLock> lock;                        // an installed folder's hold, when discovery took it
};

struct PackRefusal {
    std::string           id;  // the manifest's id, or the folder's name when the manifest did not parse
    std::filesystem::path dir;
    std::string           reason;
};

}  // namespace rp::host::packs
