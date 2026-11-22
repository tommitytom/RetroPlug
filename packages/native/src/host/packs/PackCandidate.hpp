#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "host/packs/FileLock.hpp"

// What a launch hands the loader (host/packs/PackDiscovery.hpp) and what the loader answers when it refuses
// (host/packs/PackTable.hpp): a version folder and where it came from, and a refusal with its reason.
namespace rp::host::packs {

enum class PackOrigin { Installed, Development };

// A pack the loader may load: its version folder and where it was found. A candidate found by a launch carries the
// hold on its folder the launch took while it held state.lock (loadLaunchPacks), so no other launch's settle can delete
// the folder between its discovery and its load: an installed one, or a development one that lies inside the data
// root. The loader takes a hold itself for such a candidate that has none (an installed one, or a development one
// inside the data root its table was given), and none for a development folder elsewhere.
struct PackCandidate {
    std::filesystem::path     dir;
    PackOrigin                origin = PackOrigin::Development;
    bool                      overridesInstalled = false;  // a development pack standing in for an installed one
    std::shared_ptr<FileLock> lock;                        // the hold discovery took, when it took one
    std::string               id;  // the pack id when the finder knew it (state.json entry, dev manifest)
};

struct PackRefusal {
    std::string           id;  // the manifest's id, else the candidate's, else the pack's folder name (or "state.json")
    std::filesystem::path dir;
    std::string           reason;
};

}  // namespace rp::host::packs
