#include "graph/Packages.hpp"

// The fixture package's entry, the one function its manifest would name: a product's registration holds it, and the
// branch this project compiles does not otherwise touch what a package is.
extern "C" const rp_package* rp_fixture_entry(void) { return nullptr; }
