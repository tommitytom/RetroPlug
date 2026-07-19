// The host glue that binds the app layer to the DSP runtime — the small, shared wiring every
// host (plugin, standalone, CLI) reuses on top of the store graph. Two pieces:
//
//   - buildAppRegistry() assembles the control-plane RoleRegistry: the backend "system" roles
//     (coreRoles), the DSP-thread feature behaviours (dspRoles), and the ROM providers that
//     attach those features by ROM identity (romProviders). This is the registry the stores
//     drive; the bare DSP-context bundle builds its own (dspRoles only).
//   - syncDspFromStore() projects the live systems into a KernelStructure and pushes it to the
//     DSP runtime — the callback a host installs on ProjectStore.onSystemsChange (after the DSP
//     runtime exists) so every structural edit re-drives the kernel.

import { RoleRegistry } from "./systemRoles";
import { registerCoreRoles } from "./coreRoles";
import { registerDspRoles } from "./dspRoles";
import { registerLsdjAssetsRole } from "./lsdjAssetsRole";
import { registerRisaRole } from "./risaRole";
import { registerRomProviders } from "./romProviders";
import { projectKernelStructure } from "./kernelProjection";
import type { ProjectStore } from "./projectStore";
import type { DspRuntimeClient } from "./dspRuntime";

/** The control-plane role registry: backend system roles + DSP feature behaviours + the ROM
 *  providers that attach features by ROM identity. */
export function buildAppRegistry(): RoleRegistry {
  const registry = new RoleRegistry();
  registerCoreRoles(registry);
  registerDspRoles(registry);
  registerLsdjAssetsRole(registry);
  registerRisaRole(registry);
  registerRomProviders(registry);
  return registry;
}

/** Project the store's live systems into the kernel structure and push it to the DSP runtime.
 *  Install as the ProjectStore.onSystemsChange hook so each structural edit re-drives the kernel. */
export function syncDspFromStore(project: ProjectStore, dsp: DspRuntimeClient): boolean {
  return dsp.setSystems(projectKernelStructure(project.systems.view(), project.settings().midiRouting));
}
