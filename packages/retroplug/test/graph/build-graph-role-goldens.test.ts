// buildGraph with roles: golden/build-graph-roles.json is the document buildGraph builds for each model of a matrix of
// systems with protocol nodes, controllers and MIDI routings (docs/modular-graph-phase-b3b-design.md section 4), and
// this file holds the two to each other, case for case: document, slots and tap.
//
// A sibling of golden/build-graph.json rather than more cases in it, because the two record different things.
// build-graph.json is the B1 native translator's matrix of system sets, routings and host shapes, whose audio is the
// translator's still: build-graph-goldens.test.ts holds buildGraph to it, no model in it has a role, and it has no
// recorder (an intentional change rewrites it by hand and says why). Here buildGraph is the record: the file is written
// from the cases below, so their protocol nodes, wires and the host I/O around them are re-recorded when the wiring
// changes on purpose, and the commit's diff of this file is the record of what moved. To re-record, from the
// repository root:
//
//   RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens
//
// which writes the file from the cases below instead of comparing them (GOLDEN_PATH is relative to the package, the
// runner's working directory). So a case is added or changed here and then recorded; a case in the file that is not
// here fails.
//
// packages/native/test/engine-graph/BuildGraphGoldens.test.cpp plans every document of this file on the engine with
// real cores, beside build-graph.json's, and runs it twenty blocks with the transport playing, host MIDI and a control
// surface's press staged, so CI plans buildGraph's per-role wiring on the real engine: each protocol node type against
// the system node it feeds, the routing in each mode, a controller each way, the MI.OUT feedback wire and the N8 cart
// mirror. Every document must plan, and its blocks fault no node, throw in no script node and drop no message.
import { test, expect } from "../../testing/harness";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { buildGraph, type GraphModel, type GraphSystem, type HostShape, type SystemNodeType, type TapArm } from "../../src/graph/buildGraph";
import { protocolPipeline } from "../../src/graph/protocolNodes";
import type { GraphController } from "../../src/graph/controller";
import type { RoleInstance } from "../../src/systemRoles";
import { MIDI_ROUTING_VALUES, type ControllerTarget } from "../../src/settingsEnums";
import golden from "./golden/build-graph-roles.json";

declare const tjs: { env: Record<string, string | undefined>; writeFile(path: string, data: string): Promise<void> };

const GOLDEN_PATH = "test/graph/golden/build-graph-roles.json";

// ---- the models ----------------------------------------------------------------------------------------------------

// A system's roles as a store holds them, parsed by each role's schema; its pipeline is what protocolPipeline places.
const system = (id: number, type: SystemNodeType, roles: RoleInstance[], linkGroup = 0, exportMode = 0): GraphSystem => ({
  id,
  type,
  linkGroup,
  exportMode,
  pipeline: protocolPipeline(roles),
});
const gb = (id: number, roles: RoleInstance[], linkGroup = 0) => system(id, "rp.sameboy.gb", roles, linkGroup);
const nes = (id: number, roles: RoleInstance[], exportMode = 0) => system(id, "rp.mesen.nes", roles, 0, exportMode);
const sms = (id: number, roles: RoleInstance[]) => system(id, "rp.mesen.sms", roles);

const MGB: RoleInstance = { kind: "mgb", config: {} };
const N8: RoleInstance = { kind: "nes-n8-midi", config: {} };
const RISA: RoleInstance = { kind: "risa-sync", config: {} };
const smsSync = (machine: "sms" | "gg"): RoleInstance => ({ kind: "sms-sync", config: { machine } });
const lsdj = (mode: string, tempoDivisor = 1, autoStart = false): RoleInstance => ({
  kind: "lsdj-sync",
  config: { mode, tempoDivisor, autoStart },
});

/** A song of sixteen one-phrase rows on every channel, as the control plane derives the table from a battery. */
const ROW_TICKS = [0, 1, 2, 3].map(() => Array.from({ length: 16 }, () => 96));

/** The lsdj-midimap app for `systemId` (0 for the first system), quantised to `quantise`, its config as the app's
 *  schema parses it. */
const launchpad = (
  target: ControllerTarget,
  systemId: number,
  quantise = "bar",
  over: Partial<GraphController> = {},
): GraphController => ({
  app: "lsdj-midimap",
  target,
  systemId,
  appConfig: { quantise, follow: true, page: 0 },
  songRowTicks: ROW_TICKS,
  anchor: null,
  cartSync: null,
  ...over,
});

const STEREO: HostShape = { kind: "multi", channels: 2 };

interface Case {
  name: string;
  model: GraphModel;
}

const model = (systems: GraphSystem[], over: Partial<GraphModel> = {}): GraphModel => ({
  systems,
  routing: "stereo",
  shape: STEREO,
  midiRouting: "sendToAll",
  controller: null,
  ...over,
});

// Every protocol node type on the system type it is placed for: mGB and an LSDj decoding MI.OUT on Game Boys (the
// LSDj's serial out fed back to it, its host MIDI out an input of rp.host.midi-out), the N8 and risa on a NES (which the
// cart mirrors), smsggdj on an SMS.
const everyNode = (): GraphSystem[] => [gb(1, [MGB]), gb(2, [lsdj("midiOut")]), nes(3, [N8, RISA]), sms(4, [smsSync("sms")])];

const CASES: Case[] = [
  // The routing in each mode, and none (a DirectGraph, or a store on its own, before a mode is set: no node's MIDI
  // input is wired).
  ...MIDI_ROUTING_VALUES.map((mode) => ({ name: `every-node/${mode}`, model: model(everyNode(), { midiRouting: mode }) })),
  { name: "every-node/no-routing", model: model(everyNode(), { midiRouting: null }) },
  // A controller driving the first system's cart (its lsdj-sync forced to MI.MAP, its launches wired in), beside an
  // LSDj in master sync, whose clocks go out of the host after the controller's input.
  {
    name: "controller/system/first",
    model: model([gb(1, [lsdj("midiSync")]), gb(2, [lsdj("masterSync")]), nes(3, [N8])], { controller: launchpad("system", 0) }),
  },
  // A chosen cart whose own SYNC is not MI.MAP: its lsdj-sync is forced off.
  {
    name: "controller/system/chosen-cart-off",
    model: model([gb(1, [lsdj("midiSync")]), gb(2, [lsdj("midiMap")])], { controller: launchpad("system", 2, "bar", { cartSync: "Lsdj" }) }),
  },
  // Launches out of the host to a real Game Boy: rp.host.midi-out takes the controller's input first, under system 2,
  // then each LSDj's in slot order.
  {
    name: "controller/midiOut",
    model: model([gb(1, [lsdj("midiOut")]), gb(2, [lsdj("masterSync")])], { controller: launchpad("midiOut", 2) }),
  },
  // No system to resolve "the first" to: the controller's host MIDI is tagged 4294967295, the node's system 0.
  { name: "controller/no-systems", model: model([], { controller: launchpad("system", 0) }) },
  // The protocol nodes stay in slot order while a link group moves the systems' own order; MI.OUT comes back to each
  // linked LSDj.
  {
    name: "audio/linked-lsdj/twoPerInstance/multi4",
    model: model([gb(1, [lsdj("midiSync")], 1), gb(2, [MGB]), gb(3, [lsdj("midiOut")], 1)], {
      routing: "twoPerInstance",
      shape: { kind: "multi", channels: 4 },
    }),
  },
  // A lone NES split into its channels (the tap armed), its N8 and risa nodes feeding the cart mirror too.
  {
    name: "audio/lone-nes/channelSplit/multi8",
    model: model([nes(1, [N8, RISA])], { routing: "channelSplit", shape: { kind: "multi", channels: 8 } }),
  },
  // A lone NES's pins on pairs, one pair a stream.
  { name: "audio/lone-nes/pinSplit/perChannel", model: model([nes(1, [N8])], { routing: "pinSplit", shape: { kind: "perChannel" } }) },
  // Each system on its own pair: two NES, of which the cart mirrors the first, and a Game Gear.
  {
    name: "audio/two-nes-and-gg/perSystem",
    model: model([nes(1, [N8]), nes(2, [N8, RISA]), sms(3, [smsSync("gg")])], { shape: { kind: "perSystem" } }),
  },
];

// ---- the protocol rig's structures ---------------------------------------------------------------------------------
//
// packages/native/test/engine-graph/ProtocolNodes.test.cpp, the equivalence rig, plans each case named "rig/<name>" by
// its name, as buildGraph built it but for its systems, which probes and feeds stand in for, and its audio, which it
// leaves out. So the per-role graph the rig holds to the DSP kernel's recorded digests is the one buildGraph builds, and
// a change to buildGraph's wiring for these structures fails this file until re-recorded, then the rig if it moves what
// reaches a system or the host. Each structure is the one the rig's cases describe, which say where each is used.

const RIG_CASES: Case[] = [];
const rig = (name: string, systems: GraphSystem[], midiRouting: GraphModel["midiRouting"], controller: GraphController | null = null) =>
  RIG_CASES.push({ name: `rig/${name}`, model: model(systems, { midiRouting, controller }) });

// mGB and the N8, under each routing.
for (const mode of ["sendToAll", "oneChannelPerInstance", "midiChannelToInstance", "fourChannelsPerInstance"] as const)
  rig(`mgb-and-n8/${mode}`, [gb(1, [MGB]), nes(2, [N8])], mode);
// An LSDj in each sync mode.
rig(
  "lsdj-modes",
  [
    gb(1, [lsdj("midiSync", 1, true)]),
    gb(2, [lsdj("midiSync", 2)]),
    gb(3, [lsdj("midiSyncArduinoboy", 1, true)]),
    gb(4, [lsdj("midiMap")]),
    gb(5, [lsdj("keyboardMidi")]),
    gb(6, [lsdj("midiPassthrough")]),
    gb(7, [lsdj("midiOut")]),
    gb(8, [lsdj("masterSync")]),
    gb(9, [lsdj("off")]),
  ],
  "sendToAll",
);
// A NES on the N8 and risa, smsggdj on an SMS and on a Game Gear.
rig("risa-and-sms", [nes(1, [N8, RISA]), sms(2, [smsSync("sms")]), sms(3, [smsSync("gg")])], "sendToAll");
// A controller on Game Boy 7, quantised to the bar, driving its cart's MI.MAP or a real Game Boy, beside one decoding
// MI.OUT.
for (const target of ["system", "midiOut"] as const)
  rig(`controller/${target}`, [gb(7, [lsdj("midiMap")]), gb(8, [lsdj("midiOut")])], "sendToAll", launchpad(target, 7));
// A controller on the first system, launching at once, beside an LSDj decoding MI.OUT and a master.
for (const target of ["system", "midiOut"] as const)
  rig(
    `controller-orders/${target}`,
    [gb(1, [lsdj("midiMap")]), gb(2, [lsdj("midiOut")]), gb(3, [lsdj("masterSync")])],
    "sendToAll",
    launchpad(target, 0, "immediate"),
  );
// Structure changes: two LSDj clocks and mGB, a NES added, the Arduinoboy removed and added again at the end, the first
// LSDj's clock divided with the order changed under positional routing, everything removed, and mGB back alone.
const STATE_LSDJ = gb(1, [lsdj("midiSync")]);
const STATE_MGB = gb(2, [MGB]);
const STATE_ARDUINOBOY = gb(3, [lsdj("midiSyncArduinoboy")]);
const STATE_NES = nes(4, [N8, RISA]);
rig("state/first", [STATE_LSDJ, STATE_MGB, STATE_ARDUINOBOY], "sendToAll");
rig("state/nes-added", [STATE_LSDJ, STATE_MGB, STATE_ARDUINOBOY, STATE_NES], "sendToAll");
rig("state/arduinoboy-removed", [STATE_LSDJ, STATE_MGB, STATE_NES], "sendToAll");
rig("state/arduinoboy-again", [STATE_LSDJ, STATE_MGB, STATE_NES, STATE_ARDUINOBOY], "sendToAll");
rig("state/divided-and-reordered", [gb(1, [lsdj("midiSync", 4)]), STATE_ARDUINOBOY, STATE_NES, STATE_MGB], "oneChannelPerInstance");
rig("state/mgb-alone", [STATE_MGB], "sendToAll");
// No system at all.
rig("empty", [], "sendToAll");
// mGB on either side of an LSDj, whose node the throw case makes throw.
rig("throw", [gb(1, [MGB]), gb(2, [lsdj("midiSync")]), gb(3, [MGB])], "sendToAll");
// What the restructuring stress case cycles through, with "empty".
rig("stress/lsdj-and-nes", [gb(1, [lsdj("midiSync")]), nes(2, [N8, RISA])], "sendToAll");
rig("stress/controller", [gb(1, [lsdj("midiMap")]), nes(2, [N8, RISA]), gb(3, [MGB])], "sendToAll", launchpad("system", 1));
rig("stress/mgb-by-channel", [gb(3, [MGB])], "oneChannelPerInstance");
rig("stress/controller-no-systems", [], "sendToAll", launchpad("system", 0));
// A controller on the first system, quantised to the bar, before and after a system is added behind it.
rig("controller-survives/one-cart", [gb(1, [lsdj("midiMap")])], "sendToAll", launchpad("system", 0));
rig("controller-survives/system-added", [gb(1, [lsdj("midiMap")]), gb(2, [MGB])], "sendToAll", launchpad("system", 0));

CASES.push(...RIG_CASES);

// ---- the file ------------------------------------------------------------------------------------------------------

interface GoldenCase {
  name: string;
  model: GraphModel;
  document: number;
  slots: number[];
  tapArm: TapArm | null;
}

/** What the recorder writes for CASES, laid out as build-graph.json is: each distinct document once, in the order the
 *  cases first build it, one to a line, then the cases, one to a line, each naming its document by index. */
function record(): string {
  const documents: string[] = [];
  const cases: GoldenCase[] = CASES.map(({ name, model }) => {
    const built = buildGraph(model);
    const doc = canonicalJson(built.document);
    let index = documents.indexOf(doc);
    if (index < 0) index = documents.push(doc) - 1;
    return { name, model, document: index, slots: built.slots, tapArm: built.tapArm };
  });
  return `{"documents":[\n${documents.join(",\n")}\n],"cases":[\n${cases.map((c) => canonicalJson(c)).join(",\n")}\n]}\n`;
}

const goldens = golden as unknown as { documents: unknown[]; cases: GoldenCase[] };

test("the cases are named once each", () => {
  const names = CASES.map((c) => c.name);
  expect(names.filter((n, i) => names.indexOf(n) !== i)).toEqual([]);
});

test("the goldens are what buildGraph builds for every case, and nothing else", async () => {
  const recorded = record();
  if (tjs.env.RP_GOLDEN_UPDATE === "1") {
    await tjs.writeFile(GOLDEN_PATH, recorded);
    console.log(`# recorded ${CASES.length} cases into ${GOLDEN_PATH}`);
    return;
  }
  const want = JSON.parse(recorded) as typeof goldens;
  const at = (g: typeof goldens, name: string): string => {
    const c = g.cases.find((x) => x.name === name);
    return c ? canonicalJson([c, g.documents[c.document]]) : "";
  };
  // Each case by name, so a failure says which moved; then the whole, so a stray document fails too.
  const names = [...new Set([...want.cases.map((c) => c.name), ...goldens.cases.map((c) => c.name)])];
  expect(names.filter((n) => at(want, n) !== at(goldens, n))).toEqual([]);
  expect(canonicalJson(goldens)).toBe(canonicalJson(want));
});

test("every protocol node type, both controller targets and every routing mode are planned", () => {
  // What makes the file the CI check of buildGraph's per-role wiring on the engine: each of these is in some case.
  const types = new Set<string>();
  const targets = new Set<string>();
  const modes = new Set<string>();
  for (const c of CASES) {
    for (const s of c.model.systems) for (const r of s.pipeline) types.add(`${r.node} on ${s.type}`);
    if (c.model.controller) targets.add(c.model.controller.target);
    if (c.model.midiRouting) modes.add(c.model.midiRouting);
  }
  for (const t of [
    "rp.mgb.midi on rp.sameboy.gb",
    "rp.lsdj.sync on rp.sameboy.gb",
    "rp.n8.midi-to-fifo on rp.mesen.nes",
    "rp.risa.sync on rp.mesen.nes",
    "rp.sms.sync on rp.mesen.sms",
  ])
    expect(types.has(t), t).toBe(true);
  expect([...targets].sort()).toEqual(["midiOut", "system"]);
  expect([...modes].sort()).toEqual([...MIDI_ROUTING_VALUES].sort());
});
