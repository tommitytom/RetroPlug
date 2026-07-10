// Project the app's live systems into the DSP kernel's structure. The store owns systems
// as `{ id, roles }` (SystemView); the DSP kernel consumes `KernelStructure` — a project-scope
// role list + per-system pipelines. This is the one seam that turns "what the app has" into
// "what the DSP runs", pushed to the runtime on every structure change (syncDspFromStore in
// appHost.ts). Pure and registry-free: the kernel self-guards scope (dspKernel.ts — a pipeline
// stage whose roleType is missing / project-scope / has no `dsp` is skipped), so a system's
// backend "system" role rides along as harmless dead bytes and needs no filtering here.

import type { KernelStructure } from "./dspKernel";
import type { SystemView } from "./systemsStore";

/** Build the kernel structure from the systems' roles + the project MIDI-routing mode.
 *  The project-scope `midi-routing` role is synthesized from `midiRouting` (it lives in the
 *  project settings, not on any system); each system's roles map straight into its pipeline,
 *  preserving order (the stored order is authoritative for positional routing). */
export function projectKernelStructure(views: SystemView[], midiRouting: number): KernelStructure {
  return {
    project: [{ kind: "midi-routing", config: { mode: midiRouting } }],
    systems: views.map((v) => ({ id: v.id, pipeline: v.roles })),
  };
}
