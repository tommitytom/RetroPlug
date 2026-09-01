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
import { registerRisaAssetsRole } from "./risaAssetsRole";
import { registerBlipToasterRole } from "./bliptoasterRole";
import { registerBlipToasterAssetsRole } from "./bliptoasterAssetsRole";
import { registerRomProviders } from "./romProviders";
import { projectKernelStructure, type ControllerProjection } from "./kernelProjection";
import { songRowTicksFromSav } from "./lsdj/playback/fromSav";
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
  registerRisaAssetsRole(registry);
  registerBlipToasterRole(registry);
  registerBlipToasterAssetsRole(registry);
  registerRomProviders(registry);
  return registry;
}

/** Project the store's live systems into the kernel structure and push it to the DSP runtime.
 *  Install as the ProjectStore.onSystemsChange hook so each structural edit re-drives the kernel. */
export function syncDspFromStore(project: ProjectStore, dsp: DspRuntimeClient): boolean {
  const views = project.systems.view();
  const settings = project.settings();
  return dsp.setSystems(projectKernelStructure(views, settings.midiRouting, controllerProjection(project)));
}

/** Assemble the controller role's config, including the derived song-timing table the predictor runs on.
 *
 *  The table is built HERE rather than in the role because decoding a battery is a control-plane job - the
 *  audio thread has neither the bytes nor any business running the sav codec. It is rebuilt on every
 *  structure push and is never persisted.
 *
 *  KNOWN STALENESS: edits the player makes inside LSDj itself do not push a structure change, so the table
 *  lags the cart until something else does. The LEDs then describe the song as it was, which is the
 *  predictor's standing limitation (docs/launchpad-plan.md risk 5) rather than a new one. */
function controllerProjection(project: ProjectStore): ControllerProjection | undefined {
  const controller = project.settings().controller;
  if (!controller?.enabled) return undefined;

  const views = project.systems.view();
  const systemId = controller.systemId > 0 ? controller.systemId : (views[0]?.id ?? 0);
  const table = systemId > 0 ? songRowTicksFromSav(project.systems.readSram(systemId)) : null;
  // No readable battery is not a failure: the app still launches rows, it just cannot shade the ones that
  // hold chains. An empty table reads as "no content anywhere".
  return {
    ...controller,
    songRowTicks: table ?? [],
    anchor: project.controllerStartAnchor(),
    cartSync: project.controllerCartSync(),
  };
}
