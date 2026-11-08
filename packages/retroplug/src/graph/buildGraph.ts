// The engine's graph as TS builds it (docs/modular-graph-phase-b2-design.md section 2.1): the systems in slot
// order, the project's audio routing and the host's output shape, turned into the document setGraph plans. Ahead of
// the systems, each document places the script nodes that feed them (docs/modular-graph-phase-b3b-design.md
// section 4): the MIDI routing, a controller session when one is enabled, and a protocol node for each role of each
// system that has one, wired between the host's I/O and the systems as the DSP kernel's ports were. After the
// systems, it places each role's second node where its type names one, which reads what its system sent in the same
// block: LSDj's serial-out decoder, whose host MIDI goes where the kernel's did (docs/lsdj-serial-out-design.md). The
// project's hardware is nodes too (docs/modular-graph-phase-c5-design.md): a NES whose config drives the real cart gets
// an rp.n8.nes, fanned out from the same sources its core is, and an enabled controller's session reads and writes
// rp.launchpad.surface-in / -out instead of the host's own controller staging.
// Each system node carries its core's knobs (docs/modular-graph-phase-c2-design.md section 5): its core config's
// setting fields and, on a Game Boy, the serial-out capture its LSDj's decoder needs, as settings; its gain and a NES's
// expansion volume as params. Pure, and held to test/graph/golden/build-graph.json, whose audio wiring is the B1
// native translator's, so every audio node id, the document order (which is the plan order on ties) and every
// per-lane sum is the translator's.
import { COLOR_CORRECTION_VALUES, DMG_PALETTE_VALUES, HIGHPASS_VALUES, MODEL_VALUES } from "@retroplug/sameboy/enums";
import {
  CARTRIDGE_ACCURACY_VALUES,
  CHANNEL_EXPORT_VALUES,
  FIFO_PROFILE_VALUES,
  N8_LINK_VALUES,
  REGION_VALUES,
} from "@retroplug/mesen/enums";
import { type AudioRouting } from "../settingsEnums";
import { type MidiRouting } from "@retroplug/core/midiRouting";
import { type Platform, type SystemNodeType } from "../platform";
import { resourceRef, type SystemResources } from "./systemResources";
import type { GraphDocument, NodeDoc, WireDoc } from "@retroplug/core/graphDocument";
import {
  outNodeOf,
  PROTOCOL_NODES,
  type ProtocolNodeSpec,
  type ProtocolRole,
  type SystemInput,
} from "./protocolNodes";
import {
  controllerSettings,
  controllerSyncOverride,
  controllerTarget,
  NO_SYSTEM,
  serialOutCaptureFor,
  type GraphController,
  type SerialOutCaptureMode,
} from "./controller";

/** The output shape a document is built for: the host's own outputs routed by the project's mode (multi),
 *  each system on its own pair (perSystem), or one system's streams on a pair each (perChannel). A multi
 *  shape's `channels` is a positive integer: every placement is modulo it. */
export type HostShape = { kind: "multi"; channels: number } | { kind: "perSystem" } | { kind: "perChannel" };

export interface GraphSystem {
  /** The TS system id, which its node names as `system` (and the node id as `sys-<id>`). */
  id: number;
  /** What its ROM is: it picks the node type (systemNodeTypeFor) and, for a Game Gear, the machine its node builds. */
  platform: Platform;
  /** The node type its core is placed as. */
  type: SystemNodeType;
  /** Game Boys only (ignored for any other type); 0 = unlinked. */
  linkGroup: number;
  /** NES only: the channel export mode its core is built with. */
  exportMode: number;
  /** The resources its node boots from (graph/systemResources.ts): its ROM and, when it has one, the seed its core
   *  starts with. The ids are the engine's, and the store counts the handles on them. */
  resources: SystemResources;
  /** The ROM's name for a console that stages it as a file (a Master System's battery identity): its file's stem, or an
   *  embedded ROM's marker. */
  romName: string;
  /** The roles placed as protocol nodes, in the system's order, each with the node type it is placed as and its config
   *  as that node's settings (graph/protocolNodes.ts protocolPipeline, which leaves out a role with no node and one
   *  whose node would write an input an earlier role's does; ProtocolRole says who parsed the config). */
  pipeline: ProtocolRole[];
  /** The config of the system's core-config role, the one whose kind is its core (`sameboy` or `mesen`,
   *  @retroplug/sameboy and @retroplug/mesen):
   *  where its node's setting fields and a NES's expansionVolume come from (systemKnobs), and where a NES's "drive the
   *  real cart" setting decides whether the document places its rp.n8.nes at all. Absent for a system with
   *  none (a DirectGraph's, a store's with no registry, the B1 goldens' models), whose node carries none of the live
   *  fields (its construct-time fields are still written, at the node's default), so its core keeps the settings it was
   *  built with; the params it does not name still start at their defaults at its first block. */
  coreConfig?: Readonly<Record<string, unknown>>;
  /** The system's gain in dB (CommonSettings.gainDb), its node's `gain` param. Absent places no value, which leaves
   *  the param's stream where it is: at its default, 0 dB, on a new node. */
  gainDb?: number;
}

export interface GraphModel {
  /** Slot order. */
  systems: readonly GraphSystem[];
  routing: AudioRouting;
  shape: HostShape;
  /** How the host's MIDI is fanned out to the systems. Absent or null routes none: no routing node is placed, as a
   *  DirectGraph or a lone SystemsStore publishes before anything sets a mode. */
  midiRouting?: MidiRouting | null;
  /** The control-surface session, absent or null when none is enabled. */
  controller?: GraphController | null;
}

/** The NES tap the engine arms for a lone NES under a split routing: its pins (1) or its five channels (3).
 *  (The engine skips the arm when the core is already in that mode.) */
export interface TapArm {
  id: number;
  mode: 1 | 3;
}

export interface BuiltGraph {
  document: GraphDocument;
  /** System ids in slot order: the engine's system table, which orders its snapshot publishes. */
  slots: number[];
  /** What the engine's SyncTap arms on an edit that re-arms, or null for nothing (a restore only). */
  tapArm: TapArm | null;
}

/** rp.core.add's input limit (CoreNodes.cpp): a longer sum chains adds, continuing the same left-to-right sum. */
const ADD_INPUTS = 16;

// The project's script node types, as their modules spell them (@retroplug/core's midiRoute.ts and
// controllerSession.ts, which test/graph/build-graph.test.ts holds these to).
const MIDI_ROUTE = "rp.midi.route";
const CONTROLLER_SESSION = "rp.controller.session";

interface Stream {
  port: string;
  stereo: boolean;
}

// A system's node id in the documents buildGraph builds (`sys-<id>`), which the store and `DirectGraph` address a
// system node by.
export const systemNodeId = (id: number): string => `sys-${id}`;

/** A system's protocol node: sys-<id>/<role-kind>, the main design's convention. */
const protocolId = (system: number, kind: string): string => `${systemNodeId(system)}/${kind}`;

/** The rp.n8.nes node a system is driven by: cart-<id>, so the node a document places for a system is derivable from
 *  the system's id alone (the UI stages a settings message at it, and reads its capabilities, without the document). */
export const cartNodeId = (system: number): string => `cart-${system}`;

/** Whether a system drives a physical N8: its core config's `driveRealCart` (@retroplug/mesen's Mesen role). An
 *  absent value drives one,
 *  which is what a project saved before the setting existed did - the first NES's traffic reached the cart whether or
 *  not one was plugged in - and what a DirectGraph's system, built from a spec with no core config, still does. */
export function drivesRealCartOf(config: Readonly<Record<string, unknown>> | undefined): boolean {
  return config?.driveRealCart !== false;
}

/** The system a document places an rp.n8.nes for, if any: the first NES in slot order whose core config drives the real
 *  cart (drivesRealCartOf). The type is unique, so a project has one cart and one link at most, and everything that
 *  picks the node reads it here - buildGraph's placement, the menu's card jobs, the store's live expansion volume - so
 *  the document and the UI cannot disagree about which system's cart it is. A caller holds its systems as whatever it
 *  has, with the core-config role's config as `coreConfig` (a SystemView's is roleConfig(roles, "mesen")). */
export function drivenCartSystem<T extends { platform: Platform; coreConfig?: Readonly<Record<string, unknown>> }>(
  systems: readonly T[],
): T | null {
  return systems.find((s) => s.platform === "nes" && drivesRealCartOf(s.coreConfig)) ?? null;
}

// ---- the system nodes' knobs ----------------------------------------------------------------------------------------
//
// A system node's knobs (docs/modular-graph-phase-c2-design.md section 3): the setting fields each type takes from its
// core config, under the names and in the labels the role config spells them (@retroplug/sameboy, @retroplug/mesen),
// which are what the C++
// node reads (SystemNodes.cpp), and the params it takes from the system. A value the node would refuse (another label,
// a number out of its range, another type) is left out rather than sent: the planner refuses the whole document for
// one node's settings, and a loaded project keeps its core config as written (SystemsStore.adopt), so a hand edit or a
// newer build's label would silence every system. Left out, the core keeps what it has. Params are not checked so: the
// planner clamps a param's value to its range, and rounds an int's, so any finite number is sent.

type Knob = (v: unknown) => boolean;
const label = (values: readonly string[]): Knob => (v) => typeof v === "string" && values.includes(v);
const flag: Knob = (v) => typeof v === "boolean";
const within = (min: number, max: number): Knob => (v) => typeof v === "number" && v >= min && v <= max;
// Native reads a construct-time int with static_cast<int>, so a fraction is a value its node cannot carry faithfully:
// left out like any other it would refuse, rather than silently truncated.
const intIn = (min: number, max: number): Knob => (v) => within(min, max)(v) && Number.isInteger(v);
const finite = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

/** Each system node type's setting fields from its core config, and what the node takes for each. */
const SETTING_FIELDS: Readonly<Record<SystemNodeType, Readonly<Record<string, Knob>>>> = {
  "rp.sameboy.gb": {
    model: label(MODEL_VALUES),
    fastBoot: flag,
    highpass: label(HIGHPASS_VALUES),
    colorCorrection: label(COLOR_CORRECTION_VALUES),
    dmgPalette: label(DMG_PALETTE_VALUES),
    lightTemperature: within(-1, 1), // SameBoy's own range
  },
  "rp.mesen.nes": {
    region: label(REGION_VALUES),
    removeSpriteLimit: flag,
    apuLatencyMs: within(0.25, 6),
    s5bNoise: label(CARTRIDGE_ACCURACY_VALUES),
    mmc5PhaseReset: label(CARTRIDGE_ACCURACY_VALUES),
  },
  "rp.mesen.gba": {},
  "rp.mesen.sms": {},
};

/** The params each system node type takes from its core config, beside the gain every one takes from the system. */
const CONFIG_PARAMS: Readonly<Record<SystemNodeType, readonly string[]>> = {
  "rp.sameboy.gb": [],
  "rp.mesen.nes": ["expansionVolume"],
  "rp.mesen.gba": [],
  "rp.mesen.sms": [],
};

/** A construct-time field: whether the node takes the stored value as written, and the value written when it does not.
 *  The fallback is the node's own default (SystemNodes.cpp's config structs), which the role schema fills for a fresh
 *  config (@retroplug/sameboy, @retroplug/mesen), so a document names the same value whether or not the stored config
 *  held one. */
interface ConstructField {
  takes: Knob;
  fallback: unknown;
}

/** Each node type's construct-time fields, by type id and field name. Indexed by string because a document's node
 *  `type` is one (rebuildKeyOf); a type with no construct-time field has an empty entry rather than a miss. */
type ConstructFieldTable = Readonly<Record<string, Readonly<Record<string, ConstructField>>>>;

/** Each system node type's CONSTRUCT-TIME fields from its core config: the ones its node reads once, when it builds
 *  its core at activation, so a change to one is a rebuild under a new system id rather than a document
 *  (docs/modular-graph-phase-c3-design.md section 3). Every one is written to every document, its default where the
 *  config holds no value the node takes: an absent field and one at its default are different rebuild keys, so a
 *  re-publish that flipped between them would ask for a new core under a live id. */
const CONSTRUCT_FIELDS: ConstructFieldTable = {
  "rp.sameboy.gb": {},
  "rp.mesen.nes": {
    sdRoot: { takes: (v) => typeof v === "string", fallback: "" },
    fifo: { takes: label(FIFO_PROFILE_VALUES), fallback: "instant" },
    fifoDepth: { takes: intIn(0, 1 << 20), fallback: 0 },
    fifoBytesPerSecond: { takes: intIn(0, 100_000_000), fallback: 0 },
    n8Link: { takes: label(N8_LINK_VALUES), fallback: "auto" },
    n8LinkRttUs: { takes: intIn(0, 100_000), fallback: 2200 },
    channelExportMode: { takes: label(CHANNEL_EXPORT_VALUES), fallback: "mix" },
  },
  "rp.mesen.gba": {},
  "rp.mesen.sms": { enableFm: { takes: flag, fallback: true } },
};

/** A system node's knobs: what its settings carry beside its resources and its system id (and a Game Boy's capture,
 *  which buildGraph adds from its pipeline), and its param values. `construct` are the fields its core reads once, at
 *  the build; `settings` are the ones it applies live (C2). */
export interface SystemKnobs {
  settings: Record<string, unknown>;
  construct: Record<string, unknown>;
  params: Record<string, number>;
}

// The fields of `type` that `config` holds a value for, as its node takes them: a value the node would refuse is left
// out, so one bad field never refuses the whole document.
function takenFields(
  fields: Readonly<Record<string, Knob>>,
  config: Readonly<Record<string, unknown>>,
): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [field, takes] of Object.entries(fields)) if (takes(config[field])) out[field] = config[field];
  return out;
}

// Every construct-time field of `type`, as its node builds its core: the config's value where the node takes it, the
// node's own default otherwise. A value the node would refuse takes the default here rather than an absent field, so
// the document never depends on which of the two the stored config held.
function constructFields(
  fields: Readonly<Record<string, ConstructField>>,
  config: Readonly<Record<string, unknown>>,
): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [field, { takes, fallback }] of Object.entries(fields))
    out[field] = takes(config[field]) ? config[field] : fallback;
  return out;
}

/** The knobs a system node carries for `system`: its type's setting fields as its core config holds them (each left
 *  out where absent or one the node would refuse), its construct-time fields in full (the default where the config
 *  holds none the node takes), its gain, and its type's params from its core config. The one place that says which
 *  field is which: the store reads it too, to tell a setting's edit, which publishes, from a param's, which it sets
 *  live, and a construct-time field's, which rebuilds the system (SystemsStore.setRoleConfig). */
export function systemKnobs(system: Pick<GraphSystem, "type" | "coreConfig" | "gainDb">): SystemKnobs {
  const config: Readonly<Record<string, unknown>> = system.coreConfig ?? {};
  const settings = takenFields(SETTING_FIELDS[system.type] ?? {}, config);
  const construct = constructFields(CONSTRUCT_FIELDS[system.type] ?? {}, config);
  const params: Record<string, number> = {};
  if (finite(system.gainDb)) params.gain = system.gainDb;
  for (const param of CONFIG_PARAMS[system.type] ?? []) {
    const value = config[param];
    if (finite(value)) params[param] = value;
  }
  return { settings, construct, params };
}

/** The knobs of `config` that `systemKnobs` does not carry as written for `type`, with their values: a setting field
 *  whose value the node would refuse (another label, a number out of its range, another type) is left out, a
 *  construct-time field the node would refuse takes the node's default in its place, and a param whose value is not a
 *  finite number cannot ride a document (the planner clamps and rounds any number it is given) and is left out. A
 *  caller with a config as written, not through its role's schema (SystemsStore.adopt), notes them, so a stored knob
 *  the document drops leaves a trace. */
export function droppedKnobs(type: SystemNodeType, config: Readonly<Record<string, unknown>>): [string, unknown][] {
  const settings = SETTING_FIELDS[type];
  const construct = CONSTRUCT_FIELDS[type];
  const params = CONFIG_PARAMS[type] ?? [];
  return Object.entries(config).filter(
    ([field, value]) =>
      (Object.prototype.hasOwnProperty.call(settings, field) && !settings[field](value)) ||
      (Object.prototype.hasOwnProperty.call(construct, field) && !construct[field].takes(value)) ||
      (params.includes(field) && !finite(value)),
  );
}

/** The settings a system node's core is built from once, as the node's rebuild key compares them (SystemNodes.cpp)
 *  plus the resource refs node.hpp rebuilds a moved ref by itself: its type, its system id, its resources, its ROM's
 *  name, its machine (a Game Gear's `gameGear`) and every construct-time field. A document that changes one of them
 *  under a system id an older plan still runs asks for a new core while the old one is live, which the engine refuses
 *  ("system N is live"); TS moves a rebuild to a new id, and the mock backend reads this to refuse the same. NodeDoc
 *  settings absent from the key are the live knobs and params, which a document may change under the same id. */
export function rebuildKeyOf(node: Pick<NodeDoc, "type" | "settings">): Record<string, unknown> {
  const construct = CONSTRUCT_FIELDS[node.type];
  const fields = ["system", "rom", "sram", "state", "romName", "gameGear", ...Object.keys(construct ?? {})];
  const settings = node.settings ?? {};
  const key: Record<string, unknown> = { type: node.type };
  for (const field of fields) if (Object.prototype.hasOwnProperty.call(settings, field)) key[field] = settings[field];
  return key;
}

/** The link group a system's core config names (a Game Boy's cable): its `linkGroupId`, or 0 for unlinked. */
export function linkGroupOf(config: Readonly<Record<string, unknown>> | undefined): number {
  const group = config?.linkGroupId;
  return typeof group === "number" && Number.isInteger(group) && group > 0 && group <= 255 ? group : 0;
}

/** The export mode a system's core config names (a NES's channel split), as the native enum's ordinal: the position of
 *  its label in CHANNEL_EXPORT_VALUES, or 0 (mix) where it names none this build reads. */
export function exportModeOf(config: Readonly<Record<string, unknown>> | undefined): number {
  const mode = config?.channelExportMode;
  const index = typeof mode === "string" ? (CHANNEL_EXPORT_VALUES as readonly string[]).indexOf(mode) : -1;
  return index < 0 ? 0 : index;
}

/** A system's node: its system id, the resources its core boots from, its ROM's name, its construct-time fields, its
 *  knobs and, on a Game Boy, `capture`. */
function systemNode(system: GraphSystem, capture: SerialOutCaptureMode | null): NodeDoc {
  const knobs = systemKnobs(system);
  const settings: Record<string, unknown> = {
    system: system.id,
    rom: resourceRef(system.resources.rom),
    romName: system.romName,
    ...(system.resources.sram === undefined ? {} : { sram: resourceRef(system.resources.sram) }),
    ...(system.resources.state === undefined ? {} : { state: resourceRef(system.resources.state) }),
    ...knobs.settings,
    ...knobs.construct,
  };
  if (system.platform === "gg") settings.gameGear = true;
  if (capture !== null) settings.serialOutCapture = capture;
  const node: NodeDoc = { id: systemNodeId(system.id), type: system.type, settings };
  if (Object.keys(knobs.params).length > 0) node.params = knobs.params;
  return node;
}

/** The streams a core reports in `mode` (a NES's export mode; ignored otherwise), as channelLayout() does. */
function streamsOf(type: SystemNodeType, mode: number): Stream[] {
  const mono = (port: string): Stream => ({ port, stereo: false });
  if (type === "rp.sameboy.gb") return ["ch1", "ch2", "ch3", "ch4"].map((port) => ({ port, stereo: true }));
  if (type === "rp.mesen.nes") {
    if (mode === 1) return ["pins.pulse", "pins.tnd", "pins.expansion"].map(mono);
    if (mode === 2) return ["pins.pulse", "pins.tnd", "pins.expansion", "pins.mixref"].map(mono);
    if (mode === 3) return ["channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"].map(mono);
  }
  return [{ port: "mix", stereo: true }];
}

/** rp.host.audio-out's port for the bus starting at host channel `first` (0-based), `n` channels wide. */
const outPort = (first: number, n: number): string => `out.out.${first + 1}${n === 1 ? "" : `-${first + n}`}`;

const isPins = (r: AudioRouting): boolean => r === "pinSplit" || r === "stereoPinSplit";
const isSplit = (r: AudioRouting): boolean => isPins(r) || r === "channelSplit";

/** Game Boys with a nonzero link group, bucketed in slot order; a group of fewer than two is dissolved.
 *  Indices into `systems`. */
function linkGroups(systems: readonly GraphSystem[]): number[][] {
  const groups: { group: number; members: number[] }[] = [];
  systems.forEach((s, i) => {
    if (s.type !== "rp.sameboy.gb" || s.linkGroup === 0) return;
    const g = groups.find((e) => e.group === s.linkGroup);
    if (g) g.members.push(i);
    else groups.push({ group: s.linkGroup, members: [i] });
  });
  return groups.filter((g) => g.members.length >= 2).map((g) => g.members);
}

/** The order systems are summed and stepped in: unlinked by slot, then each link group (by first appearance)
 *  with its members by slot. */
function sumOrder(systems: readonly GraphSystem[], groups: number[][]): number[] {
  const linked = new Set(groups.flat());
  const order = systems.map((_, i) => i).filter((i) => !linked.has(i));
  for (const g of groups) order.push(...g);
  return order;
}

/** The tap the engine arms for this model: a split routing with exactly one system, and that one a NES. */
function tapArmOf(model: GraphModel): TapArm | null {
  const only = model.systems.length === 1 ? model.systems[0] : undefined;
  if (!isSplit(model.routing) || !only || only.type !== "rp.mesen.nes") return null;
  return { id: only.id, mode: isPins(model.routing) ? 1 : 3 };
}

/** The export mode a NES has once the engine's SyncTap has armed or restored it. */
function nesMode(arm: TapArm | null, s: GraphSystem): number {
  return arm && arm.id === s.id ? arm.mode : s.exportMode;
}

export function buildGraph(model: GraphModel): BuiltGraph {
  const { systems, routing, shape } = model;
  // One decision for the wiring and the arm returned with it, so the two always agree.
  const tapArm = tapArmOf(model);
  const nodes: NodeDoc[] = [];
  const wires: WireDoc[] = [];
  const wire = (from: string, to: string) => wires.push({ from, to });
  // A stereo endpoint as its two mono halves, through an rp.core.split named `name`.
  const halves = (from: string, name: string): [string, string] => {
    nodes.push({ id: name, type: "rp.core.split" });
    wire(from, `${name}.in`);
    return [`${name}.out1`, `${name}.out2`];
  };

  // The hosts' inputs: the musical MIDI always, and the control surface when a controller is on - placed only then, so
  // a document with no controller holds neither half of the surface (a surface node drains the device's ring, and one
  // no session reads would take the device's traffic nowhere).
  nodes.push({ id: "midi-in", type: "rp.host.midi-in" });
  if (model.controller) nodes.push({ id: "surface-in", type: "rp.launchpad.surface-in" });
  // Every node reads only nodes placed before it in the document, but over a feedback wire, so the plan runs the nodes,
  // the script nodes among them, in document order: the planner orders nodes topologically and breaks ties by document
  // order (Planner.cpp order()), and a feedback wire orders nothing. So the nodes that feed the systems go before them,
  // and a node that reads what a system sent goes after the systems.
  //
  // The routing: the host's MIDI fanned out to the systems in slot order (positional routing reads it), an output
  // for each, sys-<id>.
  const midiRouting = model.midiRouting ?? null;
  if (midiRouting !== null) {
    nodes.push({ id: "routing", type: MIDI_ROUTE, settings: { mode: midiRouting, systems: systems.map((s) => s.id) } });
    wire("midi-in.midi", "routing.midiIn");
  }
  // Host MIDI out's inputs: the controller's first, so it wins a tie at a frame (rp.host.midi-out merges its inputs by
  // frame, the lower input on a tie), then each system's in slot order, every message under the system named beside
  // it, wherever in the document the node that sends it sits.
  const hostMidi: { from: string; system: number }[] = [];
  const controller = model.controller ?? null;
  const target = controller ? controllerTarget(controller, systems) : NO_SYSTEM;
  if (controller) {
    // Its target resolved here ("0 is the first system"), so it follows a change of first system.
    nodes.push({ id: "controller", type: CONTROLLER_SESSION, settings: controllerSettings(controller, target) });
    // The device's own side of the session: what it sent, and whether it is attached - two nodes, one each way, so
    // neither is in a cycle with the session (docs/modular-graph-phase-c5-design.md section 5).
    wire("surface-in.in", "controller.controllerIn");
    wire("surface-in.connected", "controller.controllerConnected");
    // Driving a cart, it sends no host MIDI; it is wired all the same, as the kernel tagged its MIDI out either way.
    hostMidi.push({ from: "controller.midiOut", system: target });
  }
  // The controller's cart, in the sync mode the app's manifest data asks for: the settings of that cart sync node and of
  // its serial-out decoder alone, and the capture its Game Boy runs for them, never the project's.
  const sync = controllerSyncOverride(systems, controller);
  // The nodes placed after the systems, in slot order, then pipeline order: each role's second node, and the system
  // output it reads, if any.
  const after: { node: NodeDoc; reads: string | null }[] = [];
  // Each system's protocol nodes, in slot order, then pipeline order. Their outputs feed the system's inputs of the
  // same name; what reads MIDI reads its system's routed output.
  for (const s of systems) {
    const n = systemNodeId(s.id);
    for (const role of s.pipeline) {
      const spec: ProtocolNodeSpec = PROTOCOL_NODES[role.node];
      const id = protocolId(s.id, role.kind);
      const forced = sync !== null && sync.systemId === s.id && role.node === sync.node;
      const settings = forced ? { ...role.config, mode: sync.mode } : role.config;
      nodes.push({ id, type: role.node, settings });
      if (spec.midiIn !== null && midiRouting !== null) wire(`routing.${n}`, `${id}.${spec.midiIn}`);
      // The launches follow the routed MIDI, as the kernel appended them to its target's inbox.
      if (spec.launches && controller?.target === "system" && target === s.id)
        wire("controller.launches", `${id}.launches`);
      for (const input of spec.writes) wire(`${id}.${input}`, `${n}.${input}`);
      if (spec.midiOut) hostMidi.push({ from: `${id}.midiOut`, system: s.id });
      // The role's second node takes the same settings, so the two read one mode, the controller's forced one
      // included. One that reads a serial out is placed only on a Game Boy, the one system that has one.
      const outType = outNodeOf(role.node);
      if (outType === undefined) continue;
      const out: ProtocolNodeSpec = PROTOCOL_NODES[outType];
      if (out.serialOut && s.type !== "rp.sameboy.gb") continue;
      const outId = `${id}/out`;
      after.push({ node: { id: outId, type: outType, settings }, reads: out.serialOut ? `${n}.serialOut` : null });
      if (out.midiOut) hostMidi.push({ from: `${outId}.midiOut`, system: s.id });
    }
  }
  const groups = linkGroups(systems);
  const order = sumOrder(systems, groups);
  // Each Game Boy captures its serial out for the mode its lsdj-sync node runs, the controller's forced one included,
  // so the capture and the decoder reading it change in one plan: no byte captured in one mode is decoded in the
  // other.
  for (const i of order) {
    const s = systems[i];
    const forced = sync !== null && sync.systemId === s.id ? sync.mode : null;
    nodes.push(systemNode(s, s.type === "rp.sameboy.gb" ? serialOutCaptureFor(s.pipeline, forced) : null));
  }
  for (const g of groups)
    for (let k = 1; k < g.length; ++k) {
      const from = systemNodeId(systems[g[k - 1]].id);
      const to = systemNodeId(systems[g[k]].id);
      wire(`${from}.link`, `${to}.link`);
    }
  // After the systems, so each reads its system's output of this block: a Game Boy's serial out, which LSDj's
  // decoder turns into host MIDI at each byte's frame.
  for (const a of after) {
    nodes.push(a.node);
    if (a.reads !== null) wire(a.reads, `${a.node.id}.serialOut`);
  }
  // The real cart: the first NES in slot order that drives it (drivenCartSystem, the one rule the UI picks the node by
  // too). What its protocol nodes send that system's core MIDI and core bytes, they send the cart too - fan-out, so
  // emulated, real or both is wiring - and its expansion volume is the system's own knob, the one value the emulated
  // core and the cart both take.
  const cart = drivenCartSystem(systems);
  if (cart) {
    const id = cartNodeId(cart.id);
    const expansionVolume = systemKnobs(cart).params.expansionVolume;
    nodes.push({ id, type: "rp.n8.nes", ...(expansionVolume === undefined ? {} : { params: { expansionVolume } }) });
    for (const role of cart.pipeline)
      for (const input of PROTOCOL_NODES[role.node].writes as readonly SystemInput[])
        if (input === "midi" || input === "bytes") wire(`${protocolId(cart.id, role.kind)}.${input}`, `${id}.${input}`);
  }
  // Host MIDI out, an input a producer, and the session's traffic back to the device. With no producer the systems list
  // is empty, an rp.host.midi-out with no inputs, which sends nothing: placed all the same, so a document has its host
  // outputs whatever its roles.
  nodes.push({ id: "midi-out", type: "rp.host.midi-out", settings: { systems: hostMidi.map((p) => p.system) } });
  hostMidi.forEach((p, k) => wire(p.from, `midi-out.in${k + 1}`));
  if (controller) {
    nodes.push({ id: "surface-out", type: "rp.launchpad.surface-out" });
    wire("controller.controllerOut", "surface-out.out");
  }

  const buses: number[] = [];
  if (shape.kind === "perSystem") {
    // Slot i's mix on channels 2i and 2i + 1.
    systems.forEach((s, i) => {
      wire(`${systemNodeId(s.id)}.mix`, outPort(2 * i, 2));
      buses.push(2);
    });
  } else if (shape.kind === "perChannel") {
    // One system's stream k on channels 2k and 2k + 1; a mono stream fills only 2k.
    if (systems.length === 1) {
      const s = systems[0];
      streamsOf(s.type, nesMode(tapArm, s)).forEach((stream, k) => {
        if (stream.stereo) {
          wire(`${systemNodeId(s.id)}.${stream.port}`, outPort(2 * k, 2));
          buses.push(2);
        } else {
          wire(`${systemNodeId(s.id)}.${stream.port}`, outPort(2 * k, 1));
          buses.push(1, 1);
        }
      });
    }
  } else {
    const n = shape.channels;
    const lanes: string[][] = Array.from({ length: n }, () => []);
    const addTo = (lane: number, from: string) => {
      if (lane < n) lanes[lane].push(from);
    };
    // A split holds for one system, the pin splits only on a NES, and only with room for its lanes.
    let splitValid = isSplit(routing) && systems.length === 1 && !(isPins(routing) && systems[0].type !== "rp.mesen.nes");
    let streams: Stream[] = [];
    let stride = 2;
    if (splitValid) {
      streams = streamsOf(systems[0].type, nesMode(tapArm, systems[0]));
      const allMono = streams.every((s) => !s.stereo);
      const pairPerMono = allMono && routing === "stereoPinSplit";
      stride = allMono && !pairPerMono ? 1 : 2;
      splitValid = stride * streams.length <= n;
    }
    if (splitValid) {
      // The split: stream k on lane (stride * k) mod n and, for a stereo stream, the next. A mono stream on a
      // pair (StereoPinSplit) fills both lanes.
      const s = systems[0];
      streams.forEach((stream, k) => {
        const l = (stride * k) % n;
        const from = `${systemNodeId(s.id)}.${stream.port}`;
        if (stream.stereo) {
          const [lo, ro] = halves(from, `stem-${s.id}-${k}`);
          addTo(l, lo);
          addTo((l + stride - 1) % n, ro);
        } else {
          addTo(l, from);
          if (stride === 2) addTo((l + 1) % n, from);
        }
      });
    } else {
      // Multi-out placement, every split that fell back included: each system's L then R, in sum order.
      for (const i of order) {
        const s = systems[i];
        const [lo, ro] = halves(`${systemNodeId(s.id)}.mix`, `split-${s.id}`);
        if (routing === "onePerInstance") {
          addTo(i % n, lo);
          addTo(i % n, ro);
        } else if (routing === "twoPerInstance") {
          const p = (2 * i) % n;
          addTo(p, lo);
          addTo((p + 1) % n, ro);
        } else {
          addTo(0, lo);
          addTo(1, ro);
        }
      }
    }
    // Each host channel: one contribution wired straight in, several summed by rp.core.add in order.
    for (let c = 0; c < n; ++c) {
      buses.push(1);
      const inputs = lanes[c];
      if (inputs.length === 0) continue;
      if (inputs.length === 1) {
        wire(inputs[0], outPort(c, 1));
        continue;
      }
      let prev = "";
      let at = 0;
      for (let j = 0; at < inputs.length; ++j) {
        const name = `lane-${c}${j ? `-${j}` : ""}`;
        const ins: string[] = prev ? [`${prev}.out`] : [];
        while (ins.length < ADD_INPUTS && at < inputs.length) ins.push(inputs[at++]);
        nodes.push({ id: name, type: "rp.core.add", settings: { inputs: ins.length } });
        ins.forEach((from, k) => wire(from, `${name}.in${k + 1}`));
        prev = name;
      }
      wire(`${prev}.out`, outPort(c, 1));
    }
  }
  nodes.push({ id: "out", type: "rp.host.audio-out", settings: { buses } });
  return { document: { nodes, wires }, slots: systems.map((s) => s.id), tapArm };
}
