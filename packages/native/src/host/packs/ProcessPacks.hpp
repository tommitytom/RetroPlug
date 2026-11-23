#pragma once

#include "host/packs/PackTable.hpp"

namespace rp::host::packs {

// The process's one pack table, seeded with the product's built-in packages (rp_register_packages): every Engine built
// with no table of its own reads it, so every plugin instance, engine job (render) and standalone engine in the process
// shares each load (an offline graph job builds a bare registry and registers no pack). Made at the first call, which
// any thread may make. It starts empty: a host's launch fills it (ensureLaunchPacks) and a live install adds to it (M4);
// nothing here scans a folder until a host asks, so a binary that never does reads no user directory.
PackTable& processPacks();

// The launch's load into processPacks(), once per process whoever asks first (every plugin instance's construction,
// the standalone's start, the CLI dispatcher's, the UI test harness's boot): the installed packs and
// RETROPLUG_PACK_PATH's development folders (loadLaunchPacks), each refusal printed to stderr. The CLI's --pack and
// Settings' Development packs are M4's. A test points RETROPLUG_PACKS_DIR at a scratch folder, so no test reads the
// user's packs.
void ensureLaunchPacks();

}  // namespace rp::host::packs
