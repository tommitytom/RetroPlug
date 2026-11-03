#pragma once

#include <cstddef>
#include <span>

#include "graph/rp_node.h"

// The node packages a product carries (docs/modular-graph-phase-e1-design.md section 4). A product's CMake lists the
// packages it is built with (retroplug_product, cmake/packages.cmake), and the configure generates that product's
// definition of this function from their manifests: their entries' rp_packages, in the order the product lists them,
// count written to *count, the array valid for the life of the program. The engine registers what it returns, so a
// product's node types are its CMake's choice. Defined once per product, never by a library. A binary that links the
// engine without declaring itself a product has no definition: an executable fails to link naming this function, but
// a Linux module links and fails only when loaded, so the configure refuses any such binary first
// (retroplug_check_products).
extern "C" const rp_package* const* rp_register_packages(size_t* count);

namespace rp::graph {

// rp_register_packages(), as a span.
inline std::span<const rp_package* const> productPackages() {
    std::size_t n = 0;
    const rp_package* const* packages = rp_register_packages(&n);
    return {packages, n};
}

}  // namespace rp::graph
