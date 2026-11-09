// A control-surface session as buildGraph places it (docs/modular-graph-phase-b3b-design.md section 4): what the
// control plane hands it, the system its launches are for, and the lsdj-sync mode the cart it drives must be in; and,
// beside that override, the serial-out capture a Game Boy runs for the mode its lsdj-sync node actually runs
// (docs/modular-graph-phase-c2-design.md section 5). Pure and registry-free: the publisher (ProjectStore, or a
// DirectGraph) parses the app's config with the app's own schema before it gets here, so neither buildGraph nor the
// node it places imports a zod schema.
import { type ControllerTarget } from "@retroplug/launchpad/enums";
import type { ControllerAnchor, RowTicksTable } from "@retroplug/tracker/trackerPlayback";
import { type ProtocolNodeSpec, type ProtocolRole } from "./protocolNodes";
import { CONTROLLER_SYNC, PROTOCOL_NODES } from "../generated/protocol";

/** An app's sync override, as its manifest's `protocol.apps` entry declares it: which cart sync node type the app's
 *  launches need, the mode that node is put in, and the polled SYNC value that counts as the cart listening. Data, so
 *  the planner carries no app and no zod (the app modules have both). */
export interface ControllerSyncRule {
  /** The node type the mode is set on: a system's cart sync node. */
  readonly node: string;
  /** The mode that node is put in. */
  readonly mode: string;
  /** The cart's own SYNC value that counts as listening. When it is polled as anything else the cart is put in `off`
   *  instead; a cart that cannot be read yet is assumed willing. Absent: the mode always. */
  readonly listening?: string;
  /** The mode that means "send this cart nothing", for one polled as not listening. */
  readonly off?: string;
}

/** An enabled controller: the user's choices, and what the control plane derived for it. */
export interface GraphController {
  /** Which controller app runs (launchpad's ts/apps/index.ts, by id); an id no app has runs nothing. */
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
 *  The rule is applied to the document, to the settings of that cart's two sync nodes alone (the node type the rule
 *  names and the decoder reading it), rather than written into the project, which is what makes it safe: nothing
 *  reaches the `.rplg`, and turning the controller off restores whatever the user had.
 *
 *  `off` when the CART's own SYNC says it is not in MI.MAP. That is not politeness - a cart in LSDJ (master)
 *  mode drives the link itself, so our clock bytes collide with its own and LSDj reports TOO BUSY and stops
 *  rendering properly. Measured in test-native/lsdj-sync-toggle. Better to send a cart that is not listening
 *  nothing at all. */
export function controllerSyncOverride(
  systems: readonly { id: number }[],
  controller: Pick<GraphController, "app" | "target" | "systemId" | "cartSync"> | null | undefined,
): { systemId: number; node: string; mode: string; driving: boolean } | null {
  if (!controller || controller.target !== "system") return null;
  // The app decides whether it drives a cart and what mode that cart needs, and it does so as DATA in its manifest
  // (generated/protocol.ts): which node type, which mode, and what the cart must be polled as. This module names no app,
  // no tracker and no mode.
  const rule = Object.prototype.hasOwnProperty.call(CONTROLLER_SYNC, controller.app)
    ? CONTROLLER_SYNC[controller.app] : null;
  if (!rule) return null;
  // "0 is the first system", resolved by the helper the launches are tagged with, so the mode and the launch cannot be
  // aimed at different systems.
  const systemId = controllerTarget(controller, systems);
  if (systemId === NO_SYSTEM) return null;
  const mode = syncModeFor(rule, controller.cartSync);
  // Whether that mode is what makes the app's launches WORK, which is the app's rule and not a mode string a caller may
  // compare against: an app whose "send nothing" mode is spelt differently, or that has none, still reads as not driving.
  return { systemId, node: rule.node, mode, driving: mode === rule.mode };
}

/** The mode a rule asks for, given the cart's own SYNC setting as last polled: the rule's mode unless the cart is
 *  polled as something other than what the app listens for, in which case its `off` (better to send a cart that is not
 *  listening nothing at all than bytes it takes for another master's). */
export function syncModeFor(rule: ControllerSyncRule, cartSync: string | null): string {
  if (cartSync && rule.listening !== undefined && cartSync !== rule.listening) return rule.off ?? rule.mode;
  return rule.mode;
}

/** What a Game Boy captures of its serial out for LSDj's decoder (its node's `serialOutCapture` setting, as the C++
 *  node spells it), which the node gives the core when it takes it, before the core first runs, and at the start of
 *  the first block of a plan that changes it. `listen`: capture the bytes it clocks out itself (LSDj as master sync's
 *  master). `drive`: that, and clock a transfer waiting on an external clock, capturing its bytes too (the
 *  Arduinoboy's part in LSDj MI.OUT). Only MI.OUT may drive: a stopped LSDj master, clocked, takes the idle bytes for
 *  another master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md). `off`: no capture. */
export type SerialOutCaptureMode = "off" | "listen" | "drive";

/** The serial-out capture of a Game Boy whose protocol nodes are `pipeline` (protocolPipeline's), for the mode its
 *  lsdj-sync node runs: `forced`, the controller's override of this system's mode (controllerSyncOverride), when it
 *  has one, else the role's own. Both LSDj-to-host MIDI modes need one: MI.OUT (midiOut) drives, playing the
 *  Arduinoboy that clocks LSDj as the serial slave; master sync (masterSync, SYNC=LSDJ) listens to the clock LSDj
 *  sends as the master. Every other mode is `off`, the forced ones among them (the controller's midiMap and off): the
 *  capture follows the node that decodes it, so a cart the controller drives in MI.MAP is never clocked under the
 *  bytes it is sent. And so is a pipeline that places no lsdj-sync, whatever a role left out of it says: what a
 *  capture delivers is decoded by that role's rp.lsdj.serial-out node, which is not placed, and a driving core would
 *  clock the serial port under the bytes the placed role writes. */
export function serialOutCaptureFor(
  pipeline: readonly ProtocolRole[],
  forced?: string | null,
): SerialOutCaptureMode {
  // The node that declares a capture table is the one whose decoder the pipeline places (LSDj's lsdj-sync), and the
  // table is its package's data: the mapping of a mode to what the core does is the cart's, while this runs in the
  // planner, which may carry no cart at all. A pipeline with no such node captures nothing, whatever a role left out of
  // it says - what a capture delivers is decoded by that role's node, which is not placed.
  const role = pipeline.find((r) => (PROTOCOL_NODES[r.node] as ProtocolNodeSpec).capture !== undefined);
  if (!role) return "off";
  const capture = (PROTOCOL_NODES[role.node] as ProtocolNodeSpec).capture!;
  const mode: unknown = forced ?? (role.config as { mode?: unknown }).mode;
  // An OWN key, not a bare index: the record's prototype would answer "constructor" or "toString" with a function,
  // and a mode is only ever one of the cart's own strings. (hasOwnProperty.call, because the shell's lib target is
  // below Object.hasOwn.)
  return typeof mode === "string" && Object.prototype.hasOwnProperty.call(capture, mode) ? capture[mode]! : "off";
}

/** The controller node's settings (@retroplug/launchpad/controllerSession): its system resolved (0 is the node's
 *  none), and the anchor spelt as the node takes it. */
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
