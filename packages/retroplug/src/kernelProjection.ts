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
import type { ControllerAnchor } from "./lsdj/playback/anchor";

/** What the controller role needs that project settings alone cannot give: the derived per-row timing
 *  table, and the start-edge anchor. Assembled by the caller (appHost), because reading a cart's battery
 *  and its live RAM needs the store. */
export interface ControllerProjection extends ControllerSettings {
  songRowTicks: RowTicksTable;
  /** Where the cart was seen starting on its own, or null on the hardware path / before it ever has. */
  anchor: ControllerAnchor | null;
  /** The cart's OWN SYNC setting, as last polled ("MidiMap", "Lsdj", …), or null when unreadable. */
  cartSync: string | null;
}

/** The `lsdj-sync` mode the controller needs the cart it drives to be in, and which system that is.
 *  Null when no controller is driving an emulated cart.
 *
 *  This exists because the two settings MUST agree and nothing made them. `lsdj-sync` defaults to
 *  `midiSync`, while the MI.MAP app's launches are NoteOns that only the `midiMap` translator turns into
 *  row bytes - so enabling a Launchpad on a fresh cart produced a cart being clocked for a mode it was not
 *  in, launches that went nowhere, and LSDj sitting on "WAIT". Reported from a hardware session.
 *
 *  The override is applied at PROJECTION time rather than written into the project, which is what makes it
 *  safe: nothing reaches the `.rplg`, and turning the controller off restores whatever the user had.
 *
 *  `off` when the CART's own SYNC says it is not in MI.MAP. That is not politeness - a cart in LSDJ (master)
 *  mode drives the link itself, so our clock bytes collide with its own and LSDj reports TOO BUSY and stops
 *  rendering properly. Measured in test-native/lsdj-sync-toggle. Better to send a cart that is not listening
 *  nothing at all. */
export function controllerSyncOverride(
  views: SystemView[],
  controller?: ControllerProjection,
): { systemId: number; mode: "midiMap" | "off" } | null {
  if (!controller?.enabled || controller.target !== "system" || controller.app !== "lsdj-midimap") return null;
  const systemId = controller.systemId > 0 ? controller.systemId : (views[0]?.id ?? 0);
  if (systemId <= 0) return null;
  // A cart we cannot read is assumed willing: that is the case on a freshly built system whose battery has
  // not been published yet, and refusing to drive it would be a worse guess than trying.
  const mode = controller.cartSync && controller.cartSync !== "MidiMap" ? "off" : "midiMap";
  return { systemId, mode };
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
        // The role re-anchors when the SEQ changes, so an anchor riding along on later pushes is applied
        // once rather than yanking the playhead back on every unrelated structure edit.
        anchorRows: controller.anchor?.rows ?? [],
        anchorSeq: controller.anchor?.seq ?? 0,
      },
    });
  }
  const sync = controllerSyncOverride(views, controller);
  return {
    project,
    systems: views.map((v) => ({
      id: v.id,
      pipeline: sync && sync.systemId === v.id ? withLsdjSyncMode(v.roles, sync.mode) : v.roles,
    })),
  };
}

/** `roles` with any `lsdj-sync` stage forced to `mode`. Returns the original array when there is nothing
 *  to change, so an unaffected system's pipeline keeps its identity across pushes. */
function withLsdjSyncMode(roles: RoleInstance[], mode: string): RoleInstance[] {
  if (!roles.some((r) => r.kind === "lsdj-sync")) return roles;
  return roles.map((r) => (r.kind === "lsdj-sync" ? { ...r, config: { ...r.config, mode } } : r));
}
