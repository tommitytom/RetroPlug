// The blastem package's entry (packages/blastem/package.json names it): a product registers the node types this
// returns. Empty until the console's node lands, so the package can be built and linked while its core is proved.
#include <cstdint>

#include "graph/node.hpp"

namespace rp::host {
namespace {

// No node types yet, and no zero-length array either (MSVC refuses one with C2466): the registry reads a null table
// with a count of 0, which is a package that registers nothing.
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.blastem","version":"1.0.0"})", 0, nullptr};

}  // namespace
}  // namespace rp::host

extern "C" const rp_package* rp_blastem_package(void) { return &rp::host::kPackage; }
