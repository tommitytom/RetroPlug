#pragma once

#include "graph/rp_node.h"

// The package's entry, named in packages/mesen/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp). It returns the rp_package of rp.mesen.nes, rp.mesen.gba and rp.mesen.sms
// (MesenNode.cpp).
extern "C" const rp_package* rp_mesen_package(void);
