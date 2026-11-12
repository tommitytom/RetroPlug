// A control-surface session as the graph places it (docs/modular-graph-phase-b3b-design.md section 4 and
// docs/modular-graph-phase-f5a-design.md section 6.3): the Launchpad package's placer fills the slots buildGraph holds
// for a package's own nodes, so the shell names no surface node, no session setting and no app. What it places:
//
//   inputs   rp.launchpad.surface-in, right after the host's MIDI input;
//   session  rp.controller.session and its three surface wires, after the routing node;
//   outputs  rp.launchpad.surface-out, after host MIDI out;
//   hostMidi the session's MIDI out first, so it wins a tie at a frame;
//   overrides the sync mode the app needs on the cart it drives (its lsdj-sync node), which a Game Boy's serial-out
//            capture follows;
//   wires    the launches into that cart's sync node.
//
// The sync rule per app is manifest data (a contributing package's `protocol.kinds.controllerApps` entry
// `syncOverride`), so this module names no tracker and no mode: an app declares which node type its launches need, the
// mode that node is put in, and the polled SYNC value that counts as the cart listening. The graph reads it through
// PlaceContext.kindData (generated/protocol.ts's KIND_DATA), and the menu reads the same data from the copy the kind's
// `accept` handed this package (setControllerSyncData): one shape, one reader, two carriers.
import { definePlacer, type Placement, type Placed, type PlaceContext, type PlaceSystem } from "@retroplug/core/placer";
import type { KindData } from "@retroplug/core/defineKind";
import type { ControllerAnchor, RowTicksTable } from "@retroplug/tracker/trackerPlayback";
import { type WireDoc } from "@retroplug/core/graphDocument";
import type { ControllerTarget } from "../enums";
import { CONTROLLER_SESSION_NODE_TYPE, SURFACE_IN_NODE_TYPE, SURFACE_OUT_NODE_TYPE } from "../nodeTypes";
import { parseAppConfig } from "../registry";

/** An app's sync override, as its manifest's `protocol.kinds.controllerApps` entry declares it: which cart sync node
 *  type the app's launches need, the mode that node is put in, and the polled SYNC value that counts as the cart
 *  listening. This package's own type: the kind's data is untyped, so no shell module needs it. */
export interface ControllerSyncRule {
  /** The node type the mode is set on: a system's cart sync node. */
  readonly node: string;
  /** The kind of the role that node belongs to: what a view's placed kinds can be asked, where the document's pipeline
   *  (which names the node) is not at hand. Absent: the menu cannot tell a placed cart from a stored one. */
  readonly role?: string;
  /** The mode that node is put in. */
  readonly mode: string;
  /** The cart's own SYNC value that counts as listening. When it is polled as anything else the cart is put in `off`
   *  instead; a cart that cannot be read yet is assumed willing. Absent: the mode always. */
  readonly listening?: string;
  /** The mode that means "send this cart nothing", for one polled as not listening. */
  readonly off?: string;
  /** The name of that node's setting that divides the 24-tick clock it sends the cart (LSDj's `tempoDivisor`): the
   *  session walks the same division (tickDivisorOf), so its predicted playhead stays on the cart's. Absent: the cart
   *  hears every tick. */
  readonly divisor?: string;
}

/** An enabled controller: the user's choices, and what the control plane derived for it. */
export interface GraphController {
  /** Which controller app runs (by id); an id no app has runs nothing. */
  app: string;
  /** Where its launches go: into the target cart's lsdj-sync node ("system"), or out of the host to a real Game
   *  Boy's Arduinoboy ("midiOut"). */
  target: ControllerTarget;
  /** The system they are for, as the user chose it: 0 is the first system. */
  systemId: number;
  /** The app's own config, already parsed by the app's schema (normalize below, or a publisher's own parse). */
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

/** The kind's data, as its `accept` handed this package: the MENU's read of what the graph will do
 *  (controllerSyncOverride below). The graph's own placement reads the same data through PlaceContext.kindData instead,
 *  so a planner that loaded no registry still places the override. Empty until accepted: an app with no rule drives no
 *  cart mode, which is what an app that launches out of the host wants anyway. */
let syncData: KindData = {};

/** Install the kind's data (each item's `syncOverride`, by app id), as the generated registry hands it at load.
 *  Replaces it: a reload's rules are the product's current ones. */
export function setControllerSyncData(data: KindData): void {
  syncData = data;
}

/** An app's rule from a kind's data: the item's `syncOverride`, or null when the app declares none. */
function ruleOfData(data: KindData, app: string): ControllerSyncRule | null {
  const item = Object.prototype.hasOwnProperty.call(data, app) ? data[app] : undefined;
  if (item === null || typeof item !== "object") return null;
  const rule = (item as { syncOverride?: unknown }).syncOverride;
  if (rule === null || typeof rule !== "object") return null;
  const { node, role, mode, listening, off, divisor } = rule as ControllerSyncRule;
  if (typeof node !== "string" || node === "" || typeof mode !== "string" || mode === "") return null;
  return { node, role, mode, listening, off, divisor };
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
 *  reaches the .rplg, and turning the controller off restores whatever the user had.
 *
 *  `off` when the CART's own SYNC says it is not in MI.MAP. That is not politeness - a cart in LSDJ (master)
 *  mode drives the link itself, so our clock bytes collide with its own and LSDj reports TOO BUSY and stops
 *  rendering properly. Measured in test-native/lsdj-sync-toggle. Better to send a cart that is not listening
 *  nothing at all. */
export function controllerSyncOverride(
  systems: readonly OverrideSystem[],
  controller: Pick<GraphController, "app" | "target" | "systemId" | "cartSync"> | null | undefined,
): { systemId: number; node: string; mode: string; driving: boolean } | null {
  if (!controller) return null;
  return overrideWith(ruleOfData(syncData, controller.app), systems, controller);
}

/** The app's rule for the graph's placement: the same data, read through PlaceContext.kindData. */
function ruleOf(kindData: PlaceContext["kindData"], app: string): ControllerSyncRule | null {
  return ruleOfData(kindData("controllerApps"), app);
}

/** A system as the override reads it: the placer hands its pipeline (the nodes the document places), the menu the
 *  system view's `placed` kinds. A caller with neither has no placement to ask, and the system counts as placed. */
export interface OverrideSystem {
  readonly id: number;
  readonly pipeline?: readonly { readonly node: string }[];
  readonly placed?: readonly string[];
}

/** Whether the system's graph places the node the rule sets a mode on. A stored sync role it leaves out (an mGB cart
 *  still carrying lsdj-sync after a ROM swap) has no such node, so nothing is driven and nothing is overridden. */
function placesRuleNode(rule: ControllerSyncRule, system: OverrideSystem | undefined): boolean {
  if (!system) return true;
  if (system.pipeline) return system.pipeline.some((r) => r.node === rule.node);
  if (system.placed && rule.role !== undefined) return system.placed.includes(rule.role);
  return true;
}

/** The override a rule asks for, given the controller and the systems. */
function overrideWith(
  rule: ControllerSyncRule | null,
  systems: readonly OverrideSystem[],
  controller: Pick<GraphController, "target" | "systemId" | "cartSync">,
): { systemId: number; node: string; mode: string; driving: boolean } | null {
  if (controller.target !== "system" || !rule) return null;
  // "0 is the first system", resolved by the helper the launches are tagged with, so the mode and the launch cannot be
  // aimed at different systems.
  const systemId = controllerTarget(controller, systems);
  if (systemId === NO_SYSTEM) return null;
  if (!placesRuleNode(rule, systems.find((s) => s.id === systemId))) return null;
  const mode = syncModeFor(rule, controller.cartSync);
  // Whether that mode is what makes the app's launches WORK, which is the app's rule and not a mode string a caller
  // may compare against: an app whose "send nothing" mode is spelt differently, or that has none, still reads as not
  // driving.
  return { systemId, node: rule.node, mode, driving: mode === rule.mode };
}

/** The mode a rule asks for, given the cart's own SYNC setting as last polled: the rule's mode unless the cart is
 *  polled as something other than what the app listens for, in which case its `off` (better to send a cart that is not
 *  listening nothing at all than bytes it takes for another master's). */
export function syncModeFor(rule: ControllerSyncRule, cartSync: string | null): string {
  if (cartSync && rule.listening !== undefined && cartSync !== rule.listening) return rule.off ?? rule.mode;
  return rule.mode;
}

/** What the cart a controller drives divides its 24-tick clock by: the setting the app's rule names (`divisor`), on
 *  that system's node of the rule's type, as the pipeline the placer is handed carries its config. 1 when nothing is
 *  driven, the rule names no setting, the system has no such node or the value is not a whole number of at least 1.
 *  The role's schema has already made the setting an integer in 1..8 by the time a pipeline carries it (a saved or
 *  hand-edited fraction is clamped and rounded there), so this guard only keeps a pipeline built by hand from handing
 *  the session what no role config holds; for every value a config can hold it agrees with the node's own
 *  `config.tempoDivisor || 1`. */
function tickDivisorOf(
  rule: ControllerSyncRule | null,
  sync: { systemId: number } | null,
  systems: readonly PlaceSystem[],
): number {
  if (!rule || !sync || typeof rule.divisor !== "string") return 1;
  const role = systems.find((s) => s.id === sync.systemId)?.pipeline.find((r) => r.node === rule.node);
  const value = role?.config[rule.divisor];
  return typeof value === "number" && Number.isFinite(value) && Math.floor(value) === value && value >= 1 ? value : 1;
}

/** The session node's settings (@retroplug/launchpad/controllerSession): its system resolved (0 is the node's none),
 *  the anchor spelt as the node takes it, and the divisor of the clock the cart it drives hears (tickDivisorOf). */
export function controllerSettings(
  controller: GraphController,
  target: number,
  tickDivisor: number,
): Record<string, unknown> {
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
    tickDivisor,
  };
}

/** The controller's placement (docs/modular-graph-phase-f5a-design.md section 6.3). */
export const controllerPlacer = definePlacer<GraphController>({
  key: "controller",
  // The app config as the app's schema parses it (defaults filled, values clamped), the same parse the store runs: a
  // model handed over by anything but a file (DirectGraph's, a test's) is normalized here, so the node and the menu
  // cannot disagree.
  normalize: (model) => ({ ...model, appConfig: parseAppConfig(model.app, model.appConfig) }),
  place(model, ctx): Placement {
    const systems: PlaceContext["systems"] = ctx.systems;
    const target = controllerTarget(model, systems);
    // The app's sync rule as its manifest declares it, through the context's kind data: what the generated protocol
    // module hands every placer, so the planner carries no registry and no app.
    const rule = ruleOf(ctx.kindData, model.app);
    const sync = overrideWith(rule, systems, model);
    // The device's two nodes are placed on their own sides of the session (inputs before it, outputs after), so neither
    // is in a cycle with it; the session's three input wires are its wires, pushed where the session sits.
    const inputs: Placed = { nodes: [{ id: "surface-in", type: SURFACE_IN_NODE_TYPE }], wires: [] };
    const session: Placed = {
      nodes: [
        {
          id: "controller",
          type: CONTROLLER_SESSION_NODE_TYPE,
          settings: controllerSettings(model, target, tickDivisorOf(rule, sync, systems)),
        },
      ],
      wires: [
        { from: "surface-in.in", to: "controller.controllerIn" },
        { from: "surface-in.connected", to: "controller.controllerConnected" },
        // How many messages the link lost, so the session repaints what its diff believes was sent (a full send
        // ring, a paint sent while the device was unplugged): the node reports the link's own count, which changes
        // when it does.
        { from: "surface-in.drops", to: "controller.controllerDrops" },
      ],
    };
    const outputs: Placed = {
      nodes: [{ id: "surface-out", type: SURFACE_OUT_NODE_TYPE }],
      wires: [{ from: "controller.controllerOut", to: "surface-out.out" }],
    };
    return {
      inputs,
      session,
      outputs,
      // Driving a cart, it sends no host MIDI; it is wired all the same, as the kernel tagged its MIDI out either way.
      hostMidi: [{ from: "controller.midiOut", system: target }],
      overrides: sync ? [{ system: sync.systemId, node: sync.node, settings: { mode: sync.mode } }] : [],
      wires(system: number, _role, spec, nodeId): readonly WireDoc[] {
        // The launches follow the routed MIDI, as the kernel appended them to its target's inbox: only the system they
        // are aimed at, and only a node type that takes them. The node's own id is the shell's, handed in, so this
        // placer names no id convention.
        if (spec.launches !== true || model.target !== "system" || target !== system) return [];
        return [{ from: "controller.launches", to: `${nodeId}.launches` }];
      },
    };
  },
});

export default controllerPlacer;
