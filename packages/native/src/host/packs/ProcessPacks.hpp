#pragma once

#include "host/packs/PackTable.hpp"

namespace rp::host::packs {

// The process's one pack table, seeded with the product's built-in packages (rp_register_packages): every Engine
// built with no table of its own reads it, so every plugin instance, render job and standalone engine in the process
// shares each load. Made at the first call, which any thread may make. It starts empty: a host's launch fills it
// (loadLaunchPacks) and a live install adds to it (M4), and nothing here scans a folder by itself, so a binary that
// never asks reads no user directory.
PackTable& processPacks();

}  // namespace rp::host::packs
