// The host glue every host (plugin, standalone, CLI) reuses on top of the store graph:
// buildAppRegistry() assembles the control-plane RoleRegistry: the backend "system" roles
// (coreRoles), the feature roles that have a protocol node (dspRoles) and those that have
// none (asset overrides, a ROM's facts), and the ROM providers that attach the features by ROM
// identity (romProviders). This is the registry the stores drive. The audio thread needs no
// registry: the ProjectStore publishes each role's protocol node with the graph.
//
// What it registers is the product's list, not a list here: each package's app contribution is generated into
// src/generated/packages.ts (docs/modular-graph-phase-f1-design.md section 3), in product order, and this applies
// it in that order.

import { RoleRegistry } from "./systemRoles";
import { ROLES } from "./generated/packages";
import { registerRomProviders } from "./romProviders";

/** The control-plane role registry: backend system roles + the feature roles + the ROM providers
 *  that attach features by ROM identity. */
export function buildAppRegistry(): RoleRegistry {
  const registry = new RoleRegistry();
  for (const role of ROLES) registry.registerRole(role);
  registerRomProviders(registry);
  return registry;
}
