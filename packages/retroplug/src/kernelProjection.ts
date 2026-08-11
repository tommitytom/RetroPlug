// Project the app's live systems into the DSP kernel's structure. The store owns systems
// as `{ id, roles }` (SystemView); the DSP kernel consumes `KernelStructure` — a project-scope
// role list + per-system pipelines. This is the one seam that turns "what the app has" into
// "what the DSP runs", pushed to the runtime on every structure change (syncDspFromStore in
// appHost.ts). Pure and registry-free: the kernel self-guards scope (dspKernel.ts — a pipeline
// stage whose roleType is missing / project-scope / has no `dsp` is skipped), so a system's
// backend "system" role rides along as harmless dead bytes and needs no filtering here.

import type { KernelStructure } from "./dspKernel";
import type { RoleInstance } from "./systemRoles";
import type { SystemView } from "./systemsStore";
import type { MidiRouting } from "./settingsEnums";
import type { ControllerSettings } from "./projectConfig";
import type { RowTicksTable } from "./lsdj/playback/predict";

/** What the controller role needs that project settings alone cannot give: the derived per-row timing
 *  table. Assembled by the caller (appHost), because reading a cart's battery needs the store. */
export interface ControllerProjection extends ControllerSettings {
  songRowTicks: RowTicksTable;
}

/** Build the kernel structure from the systems' roles + the project MIDI-routing mode.
 *  The project-scope `midi-routing` role is synthesized from `midiRouting` (it lives in the
 *  project settings, not on any system); each system's roles map straight into its pipeline,
 *  preserving order (the stored order is authoritative for positional routing).
 *
 *  `controller` synthesizes the `launchpad` role the same way, and being synthesized is the point: its
 *  config carries a ~1000-number derived timing table, and a SYNTHESIZED project role is never written to
 *  the `.rplg` (only the user's own choices are, under project settings). Omitted entirely when there is
 *  no controller, so a project without one runs exactly the pipeline it always did. */
export function projectKernelStructure(
  views: SystemView[],
  midiRouting: MidiRouting,
  controller?: ControllerProjection,
): KernelStructure {
  const project: RoleInstance[] = [{ kind: "midi-routing", config: { mode: midiRouting } }];
  if (controller?.enabled) {
    project.push({
      kind: "launchpad",
      config: {
        app: controller.app,
        target: controller.target,
        systemId: controller.systemId,
        appConfig: controller.appConfig,
        songRowTicks: controller.songRowTicks,
      },
    });
  }
  return { project, systems: views.map((v) => ({ id: v.id, pipeline: v.roles })) };
}
