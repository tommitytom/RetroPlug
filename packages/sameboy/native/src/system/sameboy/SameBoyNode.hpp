#pragma once

#include "graph/rp_node.h"

// The package's entry, named in packages/sameboy/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp). It returns the rp_package of rp.sameboy.gb (SameBoyNode.cpp).
extern "C" const rp_package* rp_sameboy_package(void);
