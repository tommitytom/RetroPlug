#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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

// One embedded resource a product carries (docs/modular-graph-phase-f2-design.md section 5): a package's manifest names
// it (`retroplug.embed`), the build generates its bytes into a C array, and putResource's `embedded` field looks it up
// by `id` here. A product's CMake lists the packages it carries, so an id a product does not list is not in its table.
// C-compatible, as the registration list above is: `id` is NUL-terminated, and `length` is the byte count.
typedef struct rp_embedded_resource {
    const char*          id;
    const unsigned char* bytes;
    unsigned int         length;
} rp_embedded_resource;

// The embedded resources a product carries, in the order the product lists its packages, count written to *count, the
// table valid for the life of the program: the product's generated definition, beside its rp_register_packages(). A
// desktop product that lists no package with an `embed` member answers a count of 0; a DEVICE product defines nothing
// here, since its nodes reach no embedded resource (putResource is the desktop's RPC), so device code must not call it.
extern "C" const rp_embedded_resource* rp_embedded_resources(size_t* count);

// The device's build of a product's packages, beside the desktop's in one desktop binary: the device-mode objects
// (retroplug_device_mode_objects, cmake/packages.cmake) define it, listing their renamed entries in the order the
// product lists the packages. Only a binary that links such objects has it (the plan exporter, the device-mode test).
extern "C" const rp_package* const* rp_register_device_mode_packages(size_t* count);

namespace rp::graph {

// rp_register_packages(), as a span.
inline std::span<const rp_package* const> productPackages() {
    std::size_t n = 0;
    const rp_package* const* packages = rp_register_packages(&n);
    return {packages, n};
}

// rp_embedded_resources()'s entry for `id`, or an empty span for an id the product does not carry.
inline std::span<const std::uint8_t> embeddedResource(std::string_view id) {
    std::size_t n = 0;
    const rp_embedded_resource* table = rp_embedded_resources(&n);
    for (std::size_t i = 0; i < n; ++i)
        if (id == table[i].id) return {table[i].bytes, table[i].length};
    return {};
}

// rp_register_device_mode_packages(), as a span.
inline std::span<const rp_package* const> deviceModePackages() {
    std::size_t n = 0;
    const rp_package* const* packages = rp_register_device_mode_packages(&n);
    return {packages, n};
}

}  // namespace rp::graph
