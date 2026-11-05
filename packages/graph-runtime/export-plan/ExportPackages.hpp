#pragma once

// The packages retroplug-export-plan carries, by the name a product lists them under (cmake/packages.cmake), each
// device entry with both of the tool's builds of it: the desktop's, which plans, and the device's, which sizes. Defined
// by a source CMakeLists.txt generates from the manifests, so `--packages` names exactly what a product would.

#include <cstddef>

#include "graph/rp_node.h"

struct RpExportPackage {
    const char*         name;     // the package's directory name: core, dsp, n8, device-io, devtest
    rp_package_entry_fn desktop;  // its device entry, as the desktop builds it
    rp_package_entry_fn device;   // the same entry, as the device builds it (renamed <entry>_device_mode)
};

// One row per device entry, in the tool's registration order (a package with several entries has several rows).
const RpExportPackage* rp_export_plan_packages(std::size_t* count);
