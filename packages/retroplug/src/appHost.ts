// The host glue every host (plugin, standalone, CLI) reuses on top of the store graph:
// buildAppRegistry() assembles the control-plane RoleRegistry: the backend "system" roles
// (coreRoles), the DSP-thread feature behaviours (dspRoles), and the ROM providers that
// attach those features by ROM identity (romProviders). This is the registry the stores
// drive; the kernel script node builds its own (dspRoles only). The kernel needs no other
// wiring: the ProjectStore publishes its structure with the graph.

import { RoleRegistry } from "./systemRoles";
import { registerCoreRoles } from "./coreRoles";
import { registerDspRoles } from "./dspRoles";
import { registerLsdjAssetsRole } from "./lsdjAssetsRole";
import { registerRisaRole } from "./risaRole";
import { registerRisaAssetsRole } from "./risaAssetsRole";
import { registerBlipToasterRole } from "./bliptoasterRole";
import { registerBlipToasterAssetsRole } from "./bliptoasterAssetsRole";
import { registerRomProviders } from "./romProviders";

/** The control-plane role registry: backend system roles + DSP feature behaviours + the ROM
 *  providers that attach features by ROM identity. */
export function buildAppRegistry(): RoleRegistry {
  const registry = new RoleRegistry();
  registerCoreRoles(registry);
  registerDspRoles(registry);
  registerLsdjAssetsRole(registry);
  registerRisaRole(registry);
  registerRisaAssetsRole(registry);
  registerBlipToasterRole(registry);
  registerBlipToasterAssetsRole(registry);
  registerRomProviders(registry);
  return registry;
}
