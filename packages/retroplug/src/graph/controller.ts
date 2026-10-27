// A control-surface session as buildGraph places it (docs/modular-graph-phase-b3b-design.md section 4): what the
// control plane hands it, the system its launches are for, and the lsdj-sync mode the cart it drives must be in.
// Pure and registry-free: the publisher (ProjectStore, or a DirectGraph) parses the app's config with the app's own
// schema before it gets here, so neither buildGraph nor the node it places imports a zod schema.
import type { ControllerTarget } from "../settingsEnums";
import type { RowTicksTable } from "../lsdj/playback/predict";
import type { ControllerAnchor } from "../lsdj/playback/anchor";

/** An enabled controller: the user's choices, and what the control plane derived for it. */
export interface GraphController {
  /** Which controller app runs (controller/apps); an id no app has runs nothing. */
  app: string;
  /** Where its launches go: into the target cart's lsdj-sync node ("system"), or out of the host to a real Game
   *  Boy's Arduinoboy ("midiOut"). */
  target: ControllerTarget;
  /** The system they are for, as the user chose it: 0 is the first system. */
  systemId: number;
  /** The app's own config, already parsed by the app's schema (controller/registry.ts parseAppConfig). */
  appConfig: Record<string, unknown>;
  /** The per-row timing table the predictor runs on, derived from the cart's battery. Never persisted: it rides the
   *  document, so a synthesized node is what carries it rather than the `.rplg`. */
  songRowTicks: RowTicksTable;
  /** Where the cart was seen starting on its own, or null on the hardware path / before it ever has. */
  anchor: ControllerAnchor | null;
  /** The cart's OWN SYNC setting, as last polled ("MidiMap", "Lsdj", ...), or null when unreadable. */
  cartSync: string | null;
}

/** The system a controller's host MIDI is tagged with when there is no system to resolve "the first" to: the
 *  kernel's -1, as the unsigned id rp.host.midi-out carries. */
export const NO_SYSTEM = 0xffffffff;

/** The system a controller's launches are for: the chosen one, else the first system, else NO_SYSTEM. An id that
 *  names no live system is left as it is, so its launches go nowhere rather than into whichever cart is first. */
export function controllerTarget(
  controller: Pick<GraphController, "systemId">,
  systems: readonly { id: number }[],
): number {
  if (controller.systemId > 0) return controller.systemId;
  return systems.length > 0 ? systems[0].id : NO_SYSTEM;
}

/** The `lsdj-sync` mode the controller needs the cart it drives to be in, and which system that is.
 *  Null when no controller is driving an emulated cart.
 *
 *  This exists because the two settings MUST agree and nothing made them. `lsdj-sync` defaults to
 *  `midiSync`, while the MI.MAP app's launches are NoteOns that only the `midiMap` translator turns into
 *  row bytes - so enabling a Launchpad on a fresh cart produced a cart being clocked for a mode it was not
 *  in, launches that went nowhere, and LSDj sitting on "WAIT". Reported from a hardware session.
 *
 *  The override is applied to the document, to that cart's lsdj-sync node's settings alone, rather than written
 *  into the project, which is what makes it safe: nothing reaches the `.rplg`, and turning the controller off
 *  restores whatever the user had.
 *
 *  `off` when the CART's own SYNC says it is not in MI.MAP. That is not politeness - a cart in LSDJ (master)
 *  mode drives the link itself, so our clock bytes collide with its own and LSDj reports TOO BUSY and stops
 *  rendering properly. Measured in test-native/lsdj-sync-toggle. Better to send a cart that is not listening
 *  nothing at all. */
export function controllerSyncOverride(
  systems: readonly { id: number }[],
  controller: Pick<GraphController, "app" | "target" | "systemId" | "cartSync"> | null | undefined,
): { systemId: number; mode: "midiMap" | "off" } | null {
  if (!controller || controller.target !== "system" || controller.app !== "lsdj-midimap") return null;
  const systemId = controllerTarget(controller, systems);
  if (systemId === NO_SYSTEM) return null;
  // A cart we cannot read is assumed willing: that is the case on a freshly built system whose battery has
  // not been published yet, and refusing to drive it would be a worse guess than trying.
  const mode = controller.cartSync && controller.cartSync !== "MidiMap" ? "off" : "midiMap";
  return { systemId, mode };
}

/** The controller node's settings (script/nodes/controllerSession.ts): its system resolved (0 is the node's none),
 *  and the anchor spelt as the node takes it. */
export function controllerSettings(controller: GraphController, target: number): Record<string, unknown> {
  return {
    app: controller.app,
    target: controller.target,
    systemId: target === NO_SYSTEM ? 0 : target,
    appConfig: controller.appConfig,
    songRowTicks: controller.songRowTicks,
    // The node re-anchors when the SEQ changes, so an anchor riding along on later publishes is applied once
    // rather than yanking the playhead back on every unrelated structure edit.
    anchorRows: controller.anchor?.rows ?? [],
    anchorSeq: controller.anchor?.seq ?? 0,
  };
}
