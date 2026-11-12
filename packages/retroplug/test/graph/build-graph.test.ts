// buildGraph's wiring, as explicit tables (docs/modular-graph-phase-b2-design.md section 7.2). Each host channel
// must be fed what the routers before B1 added into it, in their order, and systems must run in the order that
// engine stepped them (unlinked by slot, then each link group). These are the B1 translator's own tests,
// carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
// Then the script nodes ahead of the systems and the host's I/O around them (docs/modular-graph-phase-b3b-design.md
// section 4): the MIDI routing, the controller and each role's protocol node, wired as the DSP kernel's ports were; and
// after the systems, LSDj's serial-out decoder (docs/lsdj-serial-out-design.md section 3). And each system node's knobs
// (docs/modular-graph-phase-c2-design.md section 5): its core config's setting fields and a Game Boy's serial-out
// capture as settings, its gain and a NES's expansion volume as params.
import { test, expect } from "@retroplug/testing";
import {
  buildGraph,
  drivesRealCartOf,
  exportModeOf,
  type GraphModel,
  type GraphSystem,
  type HostShape,
} from "../../src/graph/buildGraph";
import { protocolPipeline, type LeftOutRole, type ProtocolRole } from "../../src/graph/protocolNodes";
import { serialOutCaptureFor } from "../../src/graph/serialCapture";
import { controllerSyncOverride, setControllerSyncData, type GraphController } from "@retroplug/launchpad/placer";
import { KIND_DATA } from "../../src/generated/protocol";
import { RoleRegistry } from "@retroplug/core/systemRoles";
import { registerCoreRoles } from "../_coreRoles";
import { MIDI_ROUTE_NODE_TYPE } from "@retroplug/core/midiRoute";
import { CONTROLLER_SESSION_NODE_TYPE } from "@retroplug/launchpad/controllerSession";
import { MGB_MIDI_NODE_TYPE } from "@retroplug/mgb/mgbMidiNode";
import { N8_MIDI_TO_FIFO_NODE_TYPE } from "@retroplug/n8/n8MidiToFifoNode";
import { RISA_SYNC_NODE_TYPE } from "@retroplug/risa/risaSyncNode";
import { SMS_SYNC_NODE_TYPE } from "@retroplug/smsggdj/smsSyncNode";
import { LSDJ_SYNC_NODE_TYPE } from "@retroplug/lsdj/lsdjSyncNode";
import { LSDJ_SERIAL_OUT_NODE_TYPE } from "@retroplug/lsdj/lsdjSerialOutNode";
import { type AudioRouting } from "../../src/settingsEnums";
import { LSDJ_MODE_VALUES } from "@retroplug/lsdj/enums";
import { type MidiRouting } from "@retroplug/core/midiRouting";

// A system as the store hands buildGraph one: its id doubles as its ROM resource's, so a document names `res:<id>`.
const gb = (id: number, linkGroup = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({
  id, platform: "gb", type: "rp.sameboy.gb", linkGroup, exportMode: 0, resources: { rom: id }, romName: `rom${id}`, pipeline,
});
const nes = (id: number, exportMode = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({
  id, platform: "nes", type: "rp.mesen.nes", linkGroup: 0, exportMode, resources: { rom: id }, romName: `rom${id}`, pipeline,
});
const multi = (channels: number): HostShape => ({ kind: "multi", channels });

// The host's I/O, the surface and the cart's node, and the script nodes around the systems: the B1 tables below are
// the rest, the audio. A cart node is `cart-<system>`, so the prefix test covers it whatever the system's id.
// The manifest data the placer reads, handed over as the kind's `accept` hands it over at load (the template literal
// stays the data's one home: generated/protocol.ts). The graph's own read goes through buildGraph's kindData; this is
// the menu's carrier, which controllerSyncOverride reads.
setControllerSyncData(KIND_DATA.controllerApps);

const AROUND_THE_AUDIO = new Set(["midi-in", "surface-in", "routing", "controller", "midi-out", "surface-out"]);
const aroundTheAudio = (id: string): boolean => AROUND_THE_AUDIO.has(id) || id.startsWith("cart-") || id.includes("/");

interface Project {
  midiRouting?: MidiRouting | null;
  controller?: GraphController | null;
}

function built(systems: GraphSystem[], routing: AudioRouting = "stereo", shape: HostShape = multi(2), project: Project = {}) {
  // A project's controller is one of the graph's models, under the key its placer names (the pre-split vocabulary stays
  // readable in this file's cases, as the goldens keep it).
  const { controller, ...rest } = project;
  const model: GraphModel = { systems, routing, shape, ...rest, models: controller ? { controller } : {} };
  const { document, slots, tapArm } = buildGraph(model);
  const from = (to: string): string => document.wires.find((w) => w.to === to)?.from ?? "";
  // What feeds host channel `c` (0-based), in order: the one endpoint wired straight in, or every input of
  // the rp.core.add chain that sums into it.
  const expand = (add: string, out: string[]): void => {
    for (let k = 1; ; ++k) {
      const input = from(`${add}.in${k}`);
      if (!input) return;
      if (input.startsWith("lane-")) expand(input.slice(0, input.indexOf(".")), out);
      else out.push(input);
    }
  };
  const lane = (c: number): string[] => {
    const src = from(`out.out.${c + 1}`);
    if (!src.startsWith("lane-")) return src ? [src] : [];
    const out: string[] = [];
    expand(src.slice(0, src.indexOf(".")), out);
    return out;
  };
  const nodeOrder = document.nodes.map((n) => n.id);
  const audioOrder = nodeOrder.filter((id) => !aroundTheAudio(id));
  const node = (id: string) => document.nodes.find((n) => n.id === id);
  const buses = node("out")?.settings?.buses;
  // Every source wired into `to`, feedback or not (an input takes one wire; a list shows a second as a bug).
  const into = (to: string): string[] => document.wires.filter((w) => w.to === to).map((w) => w.from);
  const wired = (from: string, to: string): boolean => document.wires.some((w) => w.from === from && w.to === to && !w.feedback);
  return { document, slots, tapArm, from, into, wired, lane, nodeOrder, audioOrder, node, buses };
}

test("the empty project is the host's I/O: the routing when a mode is set, and nothing that sends host MIDI", () => {
  const bare = built([]);
  // No controller, so no surface either: it is placed only for one, and a surface node drains a device's ring that no
  // session would read.
  expect(bare.nodeOrder).toEqual(["midi-in", "midi-out", "out"]);
  expect(bare.node("midi-out")?.settings).toEqual({ systems: [] });
  expect(bare.document.wires).toEqual([]);
  expect(bare.buses).toEqual([1, 1]);
  expect(bare.slots).toEqual([]);
  const routed = built([], "stereo", multi(2), { midiRouting: "sendToAll" });
  expect(routed.nodeOrder).toEqual(["midi-in", "routing", "midi-out", "out"]);
  expect(routed.node("routing")?.settings).toEqual({ mode: "sendToAll", systems: [] });
  expect(routed.document.wires).toEqual([{ from: "midi-in.midi", to: "routing.midiIn" }]);
});

test("Stereo sums every system into one pair, in slot order", () => {
  const g = built([gb(1), nes(2), gb(3)]);
  expect(g.audioOrder).toEqual(["sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.lane(0)).toEqual(["split-1.out1", "split-2.out1", "split-3.out1"]);
  expect(g.lane(1)).toEqual(["split-1.out2", "split-2.out2", "split-3.out2"]);
  expect(built([gb(1), gb(3)]).lane(0)).toEqual(["split-1.out1", "split-3.out1"]);
});

test("a system node names its core by its system id, and a Game Boy's its capture", () => {
  expect(built([gb(4)]).node("sys-4")).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { system: 4, rom: "res:4", romName: "rom4", serialOutCapture: "off" } });
  expect(built([nes(5)]).node("sys-5")).toEqual({ id: "sys-5", type: "rp.mesen.nes", settings: { system: 5, rom: "res:5", romName: "rom5", ...NES_CONSTRUCT } });
});

test("one system on a lane is wired straight in", () => {
  const g = built([gb(1)]);
  expect(g.lane(0)).toEqual(["split-1.out1"]);
  expect(g.from("out.out.1")).toBe("split-1.out1");
});

test("OnePerInstance puts L then R on one lane, and wraps past the outputs", () => {
  const g = built([gb(1), gb(2), gb(3)], "onePerInstance");
  // Flat, never (L0 + R0) + (L2 + R2): float addition is not associative.
  expect(g.lane(0)).toEqual(["split-1.out1", "split-1.out2", "split-3.out1", "split-3.out2"]);
  expect(g.lane(1)).toEqual(["split-2.out1", "split-2.out2"]);
});

test("TwoPerInstance gives slot i the pair 2i mod N", () => {
  const g = built([gb(1), gb(2), gb(3), gb(4), gb(5)], "twoPerInstance", multi(4));
  expect(g.lane(0)).toEqual(["split-1.out1", "split-3.out1", "split-5.out1"]);
  expect(g.lane(1)).toEqual(["split-1.out2", "split-3.out2", "split-5.out2"]);
  expect(g.lane(2)).toEqual(["split-2.out1", "split-4.out1"]);
  expect(g.lane(3)).toEqual(["split-2.out2", "split-4.out2"]);
});

test("a lane fed by more than 16 contributions chains its adds", () => {
  const ids = Array.from({ length: 20 }, (_, i) => i + 1);
  const g = built(ids.map((id) => gb(id)));
  expect(g.lane(0)).toEqual(ids.map((id) => `split-${id}.out1`));
  expect(g.from("lane-0-1.in1")).toBe("lane-0.out");
});

test("linked Game Boys run after the unlinked ones, each group in slot order", () => {
  const g = built([gb(1, 1), gb(2), gb(3, 1)]);
  expect(g.audioOrder).toEqual(["sys-2", "sys-1", "sys-3", "split-2", "split-1", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.from("sys-3.link")).toBe("sys-1.link");
  expect(g.lane(0)).toEqual(["split-2.out1", "split-1.out1", "split-3.out1"]);
  // Unlinking one of two dissolves the group.
  const apart = built([gb(1, 1), gb(2), gb(3)]);
  expect(apart.audioOrder).toEqual(["sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(apart.from("sys-3.link")).toBe("");
});

test("only Game Boys link: a link group on any other system is ignored", () => {
  const other: GraphSystem = { id: 2, platform: "nes", type: "rp.mesen.nes", linkGroup: 1, exportMode: 0, resources: { rom: 2 }, romName: "rom2", pipeline: [] };
  const g = built([gb(1, 1), other]);
  expect(g.audioOrder.slice(0, 2)).toEqual(["sys-1", "sys-2"]);
  expect(g.document.wires.some((w) => w.to.endsWith(".link"))).toBe(false);
});

test("ChannelSplit fans a Game Boy's stems, or falls back to Stereo on a narrow host", () => {
  const g = built([gb(1)], "channelSplit", multi(8));
  for (let k = 0; k < 4; ++k) {
    const stem = `stem-1-${k}`;
    expect(g.from(`${stem}.in`)).toBe(`sys-1.ch${k + 1}`);
    expect(g.lane(2 * k)).toEqual([`${stem}.out1`]);
    expect(g.lane(2 * k + 1)).toEqual([`${stem}.out2`]);
  }
  const narrow = built([gb(1)], "channelSplit", multi(4));
  expect(narrow.lane(0)).toEqual(["split-1.out1"]);
  expect(narrow.lane(2)).toEqual([]);
});

test("a single NES splits into channels, pins, or pins on pairs", () => {
  const channels = built([nes(1)], "channelSplit", multi(8));
  expect(channels.lane(0)).toEqual(["sys-1.channels.sq1"]);
  expect(channels.lane(4)).toEqual(["sys-1.channels.dmc"]);
  expect(channels.lane(5)).toEqual([]);
  const pins = built([nes(1)], "pinSplit", multi(8));
  expect(pins.lane(0)).toEqual(["sys-1.pins.pulse"]);
  expect(pins.lane(2)).toEqual(["sys-1.pins.expansion"]);
  const pairs = built([nes(1)], "stereoPinSplit", multi(8));
  expect(pairs.lane(2)).toEqual(["sys-1.pins.tnd"]);  // each pin mirrored onto both lanes of its pair
  expect(pairs.lane(3)).toEqual(["sys-1.pins.tnd"]);
  // Too narrow for its lanes: the mix feeds the host as in Stereo (the tap is still armed, the narrow-host defect).
  expect(built([nes(1)], "stereoPinSplit", multi(4)).lane(0)).toEqual(["split-1.out1"]);
  // Two systems: every split renders as Stereo.
  expect(built([nes(1), gb(2)], "channelSplit", multi(8)).lane(0)).toEqual(["split-1.out1", "split-2.out1"]);
});

test("every host lane of a lone NES is fed as the split routers fed it", () => {
  // Stream k lands on lane (stride * k) mod N, with stride 1 for mono streams (2 under StereoPinSplit, which
  // mirrors the mono stem onto both lanes of its pair); unused lanes stay empty.
  const lanes = (routing: AudioRouting) => {
    const g = built([nes(1)], routing, multi(8));
    return Array.from({ length: 8 }, (_, c) => g.lane(c));
  };
  expect(lanes("channelSplit")).toEqual([
    ["sys-1.channels.sq1"], ["sys-1.channels.sq2"], ["sys-1.channels.tri"], ["sys-1.channels.noise"],
    ["sys-1.channels.dmc"], [], [], [],
  ]);
  expect(lanes("pinSplit")).toEqual([["sys-1.pins.pulse"], ["sys-1.pins.tnd"], ["sys-1.pins.expansion"], [], [], [], [], []]);
  expect(lanes("stereoPinSplit")).toEqual([
    ["sys-1.pins.pulse"], ["sys-1.pins.pulse"], ["sys-1.pins.tnd"], ["sys-1.pins.tnd"],
    ["sys-1.pins.expansion"], ["sys-1.pins.expansion"], [], [],
  ]);
});

test("PerSystem and PerChannel lay the shapes the render calls want", () => {
  const perSystem = built([gb(1), nes(2)], "stereo", { kind: "perSystem" });
  expect(perSystem.from("out.out.1-2")).toBe("sys-1.mix");
  expect(perSystem.from("out.out.3-4")).toBe("sys-2.mix");
  expect(perSystem.buses).toEqual([2, 2]);
  // ChannelSplit arms the lone NES's five channels, and PerChannel lays the armed streams.
  const perChannel = built([nes(2)], "channelSplit", { kind: "perChannel" });
  expect(perChannel.from("out.out.1")).toBe("sys-2.channels.sq1");
  expect(perChannel.from("out.out.9")).toBe("sys-2.channels.dmc");
  expect(perChannel.buses).toEqual(Array(10).fill(1));
  // With no arm, the streams are the ones the core was built with.
  expect(built([nes(2, 1)], "stereo", { kind: "perChannel" }).buses).toEqual(Array(6).fill(1));
  expect(built([gb(1)], "stereo", { kind: "perChannel" }).buses).toEqual([2, 2, 2, 2]);
});

test("systems are placed by slot, not by their place in the sum order, past a link group", () => {
  // Slots 0 and 2 are linked, slot 1 is not: the sum order is slot 1, 0, 2, so a placement by sum position
  // would put the unlinked system on the first pair.
  const systems = [gb(1, 1), gb(2), gb(3, 1)];
  const two = built(systems, "twoPerInstance", multi(8));
  expect(two.audioOrder[0]).toBe("sys-2");
  expect([0, 1, 2, 3, 4, 5].map((c) => two.lane(c))).toEqual([
    ["split-1.out1"], ["split-1.out2"], ["split-2.out1"], ["split-2.out2"], ["split-3.out1"], ["split-3.out2"],
  ]);
  const one = built(systems, "onePerInstance", multi(8));
  expect([0, 1, 2].map((c) => one.lane(c))).toEqual([
    ["split-1.out1", "split-1.out2"], ["split-2.out1", "split-2.out2"], ["split-3.out1", "split-3.out2"],
  ]);
  const perSystem = built(systems, "stereo", { kind: "perSystem" });
  expect(["out.out.1-2", "out.out.3-4", "out.out.5-6"].map(perSystem.from)).toEqual(["sys-1.mix", "sys-2.mix", "sys-3.mix"]);
  expect(perSystem.slots).toEqual([1, 2, 3]);
});

test("the tap is armed for a lone NES under a split routing, and for nothing else", () => {
  expect(built([nes(5)], "pinSplit").tapArm).toEqual({ id: 5, mode: 1 });
  expect(built([nes(5)], "stereoPinSplit").tapArm).toEqual({ id: 5, mode: 1 });
  expect(built([nes(5)], "channelSplit").tapArm).toEqual({ id: 5, mode: 3 });
  expect(built([nes(5)], "stereo").tapArm).toBe(null);
  expect(built([gb(5)], "channelSplit").tapArm).toBe(null);
  expect(built([nes(5), nes(6)], "pinSplit").tapArm).toBe(null);
});

// --- the script nodes and the host's I/O (docs/modular-graph-phase-b3b-design.md section 4) -----------------

const role = (kind: string, config: Record<string, unknown> = {}): ProtocolRole => protocolPipeline([{ kind, config }])[0];
const MGB = role("mgb");
const N8 = role("nes-n8-midi");
const RISA = role("risa-sync");
const SMS = role("sms-sync", { machine: "gg" });
const LSDJ = (mode = "midiSync"): ProtocolRole => role("lsdj-sync", { mode, tempoDivisor: 1, autoStart: false });
const sms = (id: number, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, platform: "sms", type: "rp.mesen.sms", linkGroup: 0, exportMode: 0, resources: { rom: id }, romName: `rom${id}`, pipeline });

const CONTROLLER: GraphController = {
  app: "lsdj-midimap", target: "system", systemId: 0, appConfig: { quantise: "bar", follow: true, page: 0 },
  songRowTicks: [], anchor: null, cartSync: null,
};
const withController = (over: Partial<GraphController> = {}): Project => ({ midiRouting: "sendToAll", controller: { ...CONTROLLER, ...over } });

test("each type buildGraph places is the id its node module registers", () => {
  const types = (g: ReturnType<typeof built>) => g.document.nodes.map((n) => n.type);
  const g = built([gb(1, 0, [MGB]), nes(2, 0, [N8, RISA]), sms(3, [SMS]), gb(4, 0, [LSDJ()])], "stereo", multi(2), withController());
  for (const type of [MIDI_ROUTE_NODE_TYPE, CONTROLLER_SESSION_NODE_TYPE, MGB_MIDI_NODE_TYPE, N8_MIDI_TO_FIFO_NODE_TYPE, RISA_SYNC_NODE_TYPE, SMS_SYNC_NODE_TYPE, LSDJ_SYNC_NODE_TYPE, LSDJ_SERIAL_OUT_NODE_TYPE])
    expect(types(g).includes(type)).toBe(true);
  expect(protocolPipeline([{ kind: "sameboy", config: {} }, { kind: "lsdj-assets", config: {} }, { kind: "risa", config: {} }])).toEqual([]);
});

test("the script nodes that feed the systems go before them, in slot then pipeline order, and LSDj's decoder after them", () => {
  // Slot order, not the sum order a link group gives the systems themselves: the surface in, the routing, the
  // controller, each system's protocol nodes, the systems and their link cable, then each node that reads what a system
  // sent, before the cart node and the host outputs, and the surface out last.
  const g = built([gb(1, 1, [MGB]), nes(2, 0, [N8, RISA]), gb(3, 1, [LSDJ()])], "stereo", multi(2), withController());
  expect(g.nodeOrder.slice(0, 15)).toEqual([
    "midi-in", "surface-in", "routing", "controller", "sys-1/mgb", "sys-2/nes-n8-midi", "sys-2/risa-sync",
    "sys-3/lsdj-sync", "sys-2", "sys-1", "sys-3", "sys-3/lsdj-sync/out", "cart-2", "midi-out", "surface-out",
  ]);
  // Each protocol node takes its role's config as its settings.
  expect(g.node("sys-3/lsdj-sync")).toEqual({ id: "sys-3/lsdj-sync", type: LSDJ_SYNC_NODE_TYPE, settings: LSDJ().config });
  expect(g.node("sys-3/lsdj-sync/out")).toEqual({ id: "sys-3/lsdj-sync/out", type: LSDJ_SERIAL_OUT_NODE_TYPE, settings: LSDJ().config });
  // The link cable's wire is the systems', and the decoder's forward wire follows it.
  const wireIndex = (from: string) => g.document.wires.findIndex((w) => w.from === from);
  expect(wireIndex("sys-1.link") < wireIndex("sys-3.serialOut")).toBe(true);
});

test("the routing fans the host's MIDI out to the systems in slot order, into each node that reads it", () => {
  const g = built([gb(1, 1, [MGB]), nes(2, 0, [N8, RISA]), gb(3, 1), sms(4, [SMS]), gb(5, 0, [LSDJ()])], "stereo", multi(2), {
    midiRouting: "oneChannelPerInstance",
  });
  expect(g.node("routing")?.settings).toEqual({ mode: "oneChannelPerInstance", systems: [1, 2, 3, 4, 5] });
  expect(g.into("routing.midiIn")).toEqual(["midi-in.midi"]);
  expect(g.into("sys-1/mgb.midi")).toEqual(["routing.sys-1"]);
  expect(g.into("sys-2/nes-n8-midi.midiIn")).toEqual(["routing.sys-2"]);
  expect(g.into("sys-5/lsdj-sync.midi")).toEqual(["routing.sys-5"]);
  // A node reading only the transport takes none; a system with no node that reads it leaves its output unwired.
  expect(g.document.wires.filter((w) => w.from === "routing.sys-2").map((w) => w.to)).toEqual(["sys-2/nes-n8-midi.midiIn"]);
  expect(g.document.wires.some((w) => w.from === "routing.sys-3" || w.from === "routing.sys-4")).toBe(false);
  // With no mode there is no routing: nothing reads the host's MIDI, and no node's MIDI input is wired.
  const none = built([gb(1, 0, [MGB])]);
  expect(none.node("routing")).toBe(undefined);
  expect(none.into("sys-1/mgb.midi")).toEqual([]);
});

test("each protocol node feeds its system's inputs of the same name, and a system with none has no input wired", () => {
  const g = built([gb(1, 0, [MGB]), nes(2, 0, [N8, RISA]), sms(3, [SMS]), gb(4, 0, [LSDJ()]), gb(5)], "stereo", multi(2), { midiRouting: "sendToAll" });
  const inputs = (id: number) => ["serialIn", "midi", "bytes", "buttons"].map((p) => g.into(`sys-${id}.${p}`));
  expect(inputs(1)).toEqual([["sys-1/mgb.serialIn"], [], [], []]);
  expect(inputs(2)).toEqual([[], ["sys-2/nes-n8-midi.midi"], ["sys-2/risa-sync.bytes"], []]);
  expect(inputs(3)).toEqual([[], [], ["sys-3/sms-sync.bytes"], []]);
  expect(inputs(4)).toEqual([["sys-4/lsdj-sync.serialIn"], [], [], ["sys-4/lsdj-sync.buttons"]]);
  expect(inputs(5)).toEqual([[], [], [], []]);
});

test("of two roles whose nodes write one system input, the first in role order is placed and the later left out", () => {
  // The planner refuses a second producer into one input, and the whole document with it. A project stored with mgb
  // whose ROM path holds LSDj today gains lsdj-sync (both write serialIn), and a hand-edited file can name a kind
  // twice.
  const left: LeftOutRole[] = [];
  const roles = [
    { kind: "sameboy", config: {} },
    { kind: "mgb", config: {} },
    { kind: "lsdj-sync", config: LSDJ().config },
    { kind: "mgb", config: {} },
  ];
  const pipeline = protocolPipeline(roles, (r) => left.push(r));
  expect(pipeline).toEqual([MGB]);
  // A kind already placed is left out as such (its node id, sys-<id>/<kind>, is taken), whatever its node writes.
  expect(left).toEqual([
    { kind: "lsdj-sync", input: "serialIn", by: "mgb" },
    { kind: "mgb", input: null, by: "mgb" },
  ]);
  const g = built([gb(1, 0, pipeline)], "stereo", multi(2), { midiRouting: "sendToAll" });
  expect(g.into("sys-1.serialIn")).toEqual(["sys-1/mgb.serialIn"]);
  // The other order places lsdj-sync; nodes that write different inputs are all placed, and nothing is left out.
  expect(protocolPipeline([{ kind: "lsdj-sync", config: LSDJ().config }, { kind: "mgb", config: {} }])).toEqual([LSDJ()]);
  const none: LeftOutRole[] = [];
  const nesRoles = [{ kind: "nes-n8-midi", config: {} }, { kind: "risa-sync", config: {} }];
  expect(protocolPipeline(nesRoles, (r) => none.push(r))).toEqual([N8, RISA]);
  expect(none).toEqual([]);
});

test("a Game Boy's serial out goes forward into its LSDj's decoder, whose host MIDI is the system's input of midi-out", () => {
  // Read in the block the core captured it: a plain wire into a node placed after the system, and no feedback wire in
  // the document. The decoder writes no system input, and its rp.lsdj.sync neither reads the serial out nor sends host
  // MIDI.
  const g = built([gb(1, 0, [MGB]), gb(2, 0, [LSDJ("midiOut")]), gb(3)]);
  expect(g.document.wires.filter((w) => w.feedback)).toEqual([]);
  expect(g.document.wires.filter((w) => w.from.endsWith(".serialOut"))).toEqual([{ from: "sys-2.serialOut", to: "sys-2/lsdj-sync/out.serialOut" }]);
  expect(g.wired("sys-2.serialOut", "sys-2/lsdj-sync/out.serialOut")).toBe(true);
  const at = (id: string) => g.nodeOrder.indexOf(id);
  for (const sys of ["sys-1", "sys-2", "sys-3"]) expect(at(sys) < at("sys-2/lsdj-sync/out"), sys).toBe(true);
  expect(g.node("midi-out")?.settings).toEqual({ systems: [2] });
  expect(g.into("midi-out.in1")).toEqual(["sys-2/lsdj-sync/out.midiOut"]);
  expect(g.document.wires.filter((w) => w.from.startsWith("sys-2/lsdj-sync/out.")).map((w) => w.to)).toEqual(["midi-out.in1"]);
  expect(g.document.wires.some((w) => w.from === "sys-2/lsdj-sync.midiOut" || w.to === "sys-2/lsdj-sync.serialOut")).toBe(false);
  // Placed whatever the mode: a mode switch is a change of settings, never of the document.
  for (const mode of ["midiSync", "masterSync", "off"])
    expect(built([gb(2, 0, [LSDJ(mode)])]).node("sys-2/lsdj-sync/out")?.settings, mode).toEqual(LSDJ(mode).config);
});

test("LSDj's decoder is placed only on a Game Boy, the one system with a serial out to decode", () => {
  // A hand-edited project can hand lsdj-sync to another system: its rp.lsdj.sync is placed, and nothing reads a serial
  // out it has not got, so host MIDI out takes no input for it.
  const g = built([nes(4, 0, [LSDJ("midiOut")])]);
  expect(g.node("sys-4/lsdj-sync")?.type).toBe(LSDJ_SYNC_NODE_TYPE);
  expect(g.node("sys-4/lsdj-sync/out")).toBe(undefined);
  expect(g.node("midi-out")?.settings).toEqual({ systems: [] });
  expect(g.document.wires.some((w) => w.from.endsWith(".serialOut"))).toBe(false);
});

test("the controller takes the surface, sends it its LEDs, and its target ('0 is the first system') resolved", () => {
  const g = built([gb(7, 0, [LSDJ()]), gb(8, 0, [LSDJ()])], "stereo", multi(2), withController({ anchor: { rows: [5, 5, null, 5], seq: 3 } }));
  expect(g.node("controller")).toEqual({
    id: "controller",
    type: CONTROLLER_SESSION_NODE_TYPE,
    settings: {
      app: "lsdj-midimap", target: "system", systemId: 7, appConfig: { quantise: "bar", follow: true, page: 0 },
      songRowTicks: [], anchorRows: [5, 5, null, 5], anchorSeq: 3, tickDivisor: 1,
    },
  });
  // The device's own side: its traffic in and its connected flag, and the session's LEDs back out, each through its own
  // surface node (one node that did both would be a cycle with the session).
  expect(g.into("controller.controllerIn")).toEqual(["surface-in.in"]);
  expect(g.into("controller.controllerConnected")).toEqual(["surface-in.connected"]);
  expect(g.into("surface-out.out")).toEqual(["controller.controllerOut"]);
  expect([g.node("surface-in")?.type, g.node("surface-out")?.type]).toEqual([
    "rp.launchpad.surface-in", "rp.launchpad.surface-out",
  ]);
  // Its launches join the target cart's routed MIDI, and no other cart's.
  expect(g.into("sys-7/lsdj-sync.launches")).toEqual(["controller.launches"]);
  expect(g.into("sys-8/lsdj-sync.launches")).toEqual([]);
  // A chosen system is taken as it is.
  const chosen = built([gb(7, 0, [LSDJ()]), gb(8, 0, [LSDJ()])], "stereo", multi(2), withController({ systemId: 8 }));
  expect(chosen.into("sys-8/lsdj-sync.launches")).toEqual(["controller.launches"]);
  expect(chosen.into("sys-7/lsdj-sync.launches")).toEqual([]);
  // Without a controller there is no session and no surface: neither node is placed, so nothing drains a device.
  const without = built([gb(7, 0, [LSDJ()])]);
  expect(without.node("controller")).toBe(undefined);
  expect(without.node("surface-in")).toBe(undefined);
  expect(without.node("surface-out")).toBe(undefined);
});

test("the session is handed the divisor its cart's clock is divided by, read from that cart's node", () => {
  // MI.MAP clocks a cart at 24 / tempoDivisor ticks a quarter, so the session walks the same division (tickDivisor).
  // The app's rule names the setting (its manifest's `divisor`) and the placer reads it off the pipeline it is handed,
  // so no shell module names it; what it cannot read is 1, as the node's own `config.tempoDivisor || 1` reads it.
  const divided = (tempoDivisor: unknown): ProtocolRole =>
    role("lsdj-sync", { mode: "midiSync", tempoDivisor, autoStart: false });
  const tickDivisor = (systems: GraphSystem[], over: Partial<GraphController> = {}): unknown =>
    built(systems, "stereo", multi(2), withController(over)).node("controller")?.settings?.tickDivisor;
  expect(tickDivisor([gb(7, 0, [divided(4)]), gb(8, 0, [LSDJ()])])).toBe(4);
  // The cart the session drives, chosen or first, and no other.
  expect(tickDivisor([gb(7, 0, [LSDJ()]), gb(8, 0, [divided(2)])], { systemId: 8 })).toBe(2);
  expect(tickDivisor([gb(7, 0, [divided(4)]), gb(8, 0, [LSDJ()])], { systemId: 8 })).toBe(1);
  // No node of the rule's type on that cart, no such setting, or a value no role config holds (the schema keeps the
  // setting a whole number in 1..8), which reads as 1: 0.5 and 2.5 are fractions, not the node's clock.
  expect(tickDivisor([gb(7, 0, [MGB])])).toBe(1);
  expect(tickDivisor([gb(7, 0, [role("lsdj-sync", { mode: "midiSync" })])])).toBe(1);
  for (const bad of [0, 0.5, 2.5, -2, "2", null]) expect(tickDivisor([gb(7, 0, [divided(bad)])]), String(bad)).toBe(1);
  // Launches out of the host drive no emulated cart, and a real Game Boy's divisor is not one the graph can read.
  expect(tickDivisor([gb(7, 0, [divided(4)])], { target: "midiOut" })).toBe(1);
});

test("the controller's launches reach only a 'system' target's lsdj-sync node", () => {
  // Bound for a real Game Boy, they leave the host instead; a target cart with no lsdj-sync node takes them nowhere,
  // its mGB or N8 node included; a chosen id that names no system routes them nowhere.
  const out = built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ target: "midiOut" }));
  expect(out.into("sys-1/lsdj-sync.launches")).toEqual([]);
  const mgb = built([gb(1, 0, [MGB])], "stereo", multi(2), withController());
  expect(mgb.document.wires.some((w) => w.from === "controller.launches")).toBe(false);
  const gone = built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ systemId: 9 }));
  expect(gone.document.wires.some((w) => w.from === "controller.launches")).toBe(false);
  expect(gone.node("controller")?.settings?.systemId).toBe(9);
});

test("host MIDI out takes the controller's first, then each LSDj decoder's in slot order, each under its system", () => {
  const g = built([gb(3, 1, [LSDJ("midiOut")]), gb(1, 0, [MGB]), gb(2, 1, [LSDJ("masterSync")])], "stereo", multi(2), withController({ systemId: 2 }));
  expect(g.node("midi-out")?.settings).toEqual({ systems: [2, 3, 2] });
  expect([1, 2, 3].map((k) => g.into(`midi-out.in${k}`))).toEqual([["controller.midiOut"], ["sys-3/lsdj-sync/out.midiOut"], ["sys-2/lsdj-sync/out.midiOut"]]);
  // The decoders in slot order too, after the systems the link group reorders.
  expect(g.nodeOrder.filter((id) => id.endsWith("/out"))).toEqual(["sys-3/lsdj-sync/out", "sys-2/lsdj-sync/out"]);
  expect(g.nodeOrder.indexOf("sys-3/lsdj-sync/out") > g.nodeOrder.indexOf("sys-2")).toBe(true);
  // With no system to resolve "the first" to, the controller's is tagged as the kernel tagged it (its -1), and the node
  // takes 0, its none.
  const empty = built([], "stereo", multi(2), withController());
  expect(empty.node("midi-out")?.settings).toEqual({ systems: [0xffffffff] });
  expect(empty.node("controller")?.settings?.systemId).toBe(0);
  // Nothing that sends host MIDI: an input for nothing.
  expect(built([gb(1, 0, [MGB])]).node("midi-out")?.settings).toEqual({ systems: [] });
});

test("the cart node is the first NES in slot order that drives one, fanned out from what feeds its core", () => {
  const g = built([gb(1), nes(9, 0, [N8, RISA]), nes(3, 0, [N8])]);
  expect(g.node("cart-9")).toEqual({ id: "cart-9", type: "rp.n8.nes" });
  expect(g.into("cart-9.midi")).toEqual(["sys-9/nes-n8-midi.midi"]);
  expect(g.into("cart-9.bytes")).toEqual(["sys-9/risa-sync.bytes"]);
  // The emulated node takes the same wires: emulated, real or both is plain fan-out.
  expect(g.into("sys-9.midi")).toEqual(["sys-9/nes-n8-midi.midi"]);
  expect(g.into("sys-9.bytes")).toEqual(["sys-9/risa-sync.bytes"]);
  // A NES with no risa-sync sends its cart no bytes; one with no protocol node at all, nothing.
  expect(built([nes(4, 0, [N8])]).into("cart-4.bytes")).toEqual([]);
  expect(built([nes(4)]).document.wires.some((w) => w.to.startsWith("cart-"))).toBe(false);
  // A system that does not drive one gets no node (and only one NES ever does: the type is unique).
  expect(built([gb(1), gb(2)]).nodeOrder.some((id) => id.startsWith("cart-"))).toBe(false);
  expect(built([]).nodeOrder.some((id) => id.startsWith("cart-"))).toBe(false);
});

test("two NESes that both drive a cart still give one cart node, the first in slot order", () => {
  const both = [nes(4, 0, [N8]), nes(7, 0, [N8])].map((s) => ({ ...s, coreConfig: { driveRealCart: true } }));
  const g = built(both);
  expect(g.nodeOrder.filter((id) => id.startsWith("cart-"))).toEqual(["cart-4"]);
  expect(g.into("sys-7.midi")).toEqual(["sys-7/nes-n8-midi.midi"]); // the emulated core still hears its own
  expect(g.document.wires.some((w) => w.to.startsWith("cart-7"))).toBe(false);
  // The first NES letting go hands the cart to the next one that drives one.
  const handed = [{ ...both[0], coreConfig: { driveRealCart: false } }, both[1]];
  expect(built(handed).nodeOrder.filter((id) => id.startsWith("cart-"))).toEqual(["cart-7"]);
  expect(built(handed).document.wires.some((w) => w.to.startsWith("cart-4"))).toBe(false);
  // One that turns it off and no other NES drives one: no cart at all.
  const off = built([{ ...both[0], coreConfig: { driveRealCart: false } }]);
  expect(off.nodeOrder.some((id) => id.startsWith("cart-"))).toBe(false);
});

test("a platform with no registered cart has nothing to opt out of, so it drives one", () => {
  // The GBA registers no cart, so the NES's opt-out field in its mesen config says nothing about it: the old walk of
  // the cart list skipped every other platform and answered true, which is what the narrowing form must still say.
  expect(drivesRealCartOf({ driveRealCart: false }, "gba")).toBe(true);
  expect(drivesRealCartOf({}, "gba")).toBe(true);
  expect(drivesRealCartOf(undefined, "gba")).toBe(true);
  // The platform that does have one reads its own opt-out, which the list-walk form agrees with.
  expect(drivesRealCartOf({ driveRealCart: false }, "nes")).toBe(false);
  expect(drivesRealCartOf({ driveRealCart: false })).toBe(false);
  expect(drivesRealCartOf({ driveRealCart: true }, "nes")).toBe(true);
});

test("a cart node carries the system's expansion volume param, the same value its emulated node gets", () => {
  const withKnob = (volume: number) =>
    built([{ ...nes(2, 0, [N8]), coreConfig: { expansionVolume: volume } }]);
  expect(withKnob(40).node("cart-2")?.params).toEqual({ expansionVolume: 40 });
  expect(withKnob(40).node("sys-2")?.params).toEqual({ expansionVolume: 40 });
  // A config that names none: neither node carries the param, so both run at the node's own default.
  expect(built([nes(2, 0, [N8])]).node("cart-2")?.params).toBe(undefined);
});

// --- the cart's sync mode under a controller ----------------------------------------------------------------
//
// The two settings have to agree and nothing made them. `lsdj-sync` defaults to `midiSync`, while the MI.MAP app's
// launches are NoteOns that only the `midiMap` translator turns into row bytes - so enabling a Launchpad on a fresh
// cart clocked it for a mode it was not in, sent launches nowhere, and left LSDj sitting on "WAIT". Reported from a
// hardware session. The document forces it in that cart's lsdj-sync node alone, so nothing reaches the `.rplg`.

// Its decoder, after the Game Boy, takes the same settings, so the two always read one mode: syncModeOf holds them to
// each other.
const syncModeOf = (g: ReturnType<typeof built>, id: number): unknown => {
  const mode = g.node(`sys-${id}/lsdj-sync`)?.settings?.mode;
  expect(g.node(`sys-${id}/lsdj-sync/out`)?.settings?.mode, `sys-${id}'s decoder`).toBe(mode);
  return mode;
};

test("an enabled controller drives its cart in MI.MAP, whatever the stored mode said", () => {
  const g = built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ cartSync: "MidiMap" }));
  expect(syncModeOf(g, 1)).toBe("midiMap");
  expect(g.node("sys-1/lsdj-sync")?.settings).toEqual({ mode: "midiMap", tempoDivisor: 1, autoStart: false });
  expect(g.node("sys-1/lsdj-sync/out")?.settings).toEqual({ mode: "midiMap", tempoDivisor: 1, autoStart: false });
});

test("systemId 0 means the first system, matching what the node resolves", () => {
  const g = built([gb(7, 0, [LSDJ()]), gb(8, 0, [LSDJ()])], "stereo", multi(2), withController({ systemId: 0 }));
  expect(syncModeOf(g, 7)).toBe("midiMap");
  expect(syncModeOf(g, 8)).toBe("midiSync"); // a peer cart is left entirely alone
});

test("a cart in some OTHER sync mode is not driven at all", () => {
  // Not politeness: a cart in LSDJ (master) mode drives the link itself, so our clock bytes collide with
  // its own and LSDj reports TOO BUSY and stops rendering properly.
  expect(syncModeOf(built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ cartSync: "Lsdj" })), 1)).toBe("off");
});

test("an unread battery is assumed willing rather than refused", () => {
  // The case on a freshly built system whose battery has not been published yet.
  expect(syncModeOf(built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ cartSync: null })), 1)).toBe("midiMap");
});

test("nothing is overridden without a controller, or when it drives real hardware, or for another app", () => {
  expect(syncModeOf(built([gb(1, 0, [LSDJ()])]), 1)).toBe("midiSync");
  // target midiOut is the real-Game-Boy path: there is no emulated cart to configure, and the emulated one
  // sitting in the project is somebody else's.
  expect(syncModeOf(built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ target: "midiOut" })), 1)).toBe("midiSync");
  expect(syncModeOf(built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ app: "another" })), 1)).toBe("midiSync");
  expect(controllerSyncOverride([], CONTROLLER)).toBe(null); // no cart to drive
});

test("the sync override is the app's DATA: a node type, a mode, and what counts as listening", () => {
  // Nothing in the shell names an app, a mode or a node type: the bridge's manifest carries them
  // (protocol.kinds.controllerApps[].syncOverride), the generator renders them into generated/protocol.ts, and this is
  // the whole rule. A second app with a different cart sync node type is a manifest entry, not a branch here.
  expect(Object.keys(KIND_DATA.controllerApps)).toEqual(["lsdj-midimap"]);
  expect(KIND_DATA.controllerApps["lsdj-midimap"])
    .toEqual({
      syncOverride: {
        node: "rp.lsdj.sync", role: "lsdj-sync", mode: "midiMap", listening: "MidiMap", off: "off",
        divisor: "tempoDivisor",
      },
    });
  // `driving` is the app's own answer - the mode its launches need is the one the cart runs - and not "the mode is not
  // the string off": a second app's off-mode is its manifest's to spell.
  expect(controllerSyncOverride([{ id: 7 }], { ...CONTROLLER, cartSync: "MidiMap" }))
    .toEqual({ systemId: 7, node: "rp.lsdj.sync", mode: "midiMap", driving: true });
  expect(controllerSyncOverride([{ id: 7 }], { ...CONTROLLER, cartSync: "Lsdj" }))
    .toEqual({ systemId: 7, node: "rp.lsdj.sync", mode: "off", driving: false });
});

test("a stored sync role the system's graph does not place is not driven: no override", () => {
  // An mGB cart that still stores lsdj-sync after a ROM swap places mgb and leaves the sync node out, so there is no
  // node of the rule's type to put in a mode. The placer asks its pipeline; the menu asks the view's placed kinds.
  const driven = { ...CONTROLLER, cartSync: "MidiMap" };
  expect(controllerSyncOverride([{ id: 7, pipeline: [MGB] }], driven)).toBe(null);
  expect(controllerSyncOverride([{ id: 7, pipeline: [LSDJ()] }], driven))
    .toEqual({ systemId: 7, node: "rp.lsdj.sync", mode: "midiMap", driving: true });
  expect(controllerSyncOverride([{ id: 7, placed: ["sameboy", "mgb"] }], driven)).toBe(null);
  expect(controllerSyncOverride([{ id: 7, placed: ["sameboy", "lsdj-sync", "lsdj-assets"] }], driven))
    .toEqual({ systemId: 7, node: "rp.lsdj.sync", mode: "midiMap", driving: true });
});

test("a controller aimed at a system that is not there drives nothing: no override", () => {
  // The placer would ignore the override of a system it does not place; the menu must not say it is driving one.
  const driven = { ...CONTROLLER, cartSync: "MidiMap", systemId: 9 };
  expect(controllerSyncOverride([{ id: 7 }], driven)).toBe(null);
  expect(controllerSyncOverride([{ id: 7, pipeline: [LSDJ()] }], driven)).toBe(null);
  expect(controllerSyncOverride([{ id: 7, placed: ["lsdj-sync"] }], driven)).toBe(null);
  expect(controllerSyncOverride([], driven)).toBe(null);
});

test("the override leaves the model's own pipeline as it was", () => {
  // The whole reason it happens in the document: turning the controller off restores whatever the user had.
  const systems = [gb(1, 0, [LSDJ()])];
  expect(syncModeOf(built(systems, "stereo", multi(2), withController({ cartSync: "MidiMap" })), 1)).toBe("midiMap");
  expect(systems[0].pipeline[0].config.mode).toBe("midiSync");
  expect(syncModeOf(built(systems), 1)).toBe("midiSync");
});

// --- the system nodes' knobs (docs/modular-graph-phase-c2-design.md sections 3 and 5) ----------------------------

// A core-config role's config as a store holds it, its role's schema parsed (@retroplug/sameboy, @retroplug/mesen):
// every field filled.
const CORE_ROLES = new RoleRegistry();
registerCoreRoles(CORE_ROLES);
const coreConfig = (kind: "sameboy" | "mesen", over: Record<string, unknown> = {}): Record<string, unknown> =>
  CORE_ROLES.roleType(kind)!.schema.parse(over) as Record<string, unknown>;
const gba = (id: number): GraphSystem => ({ id, platform: "gba", type: "rp.mesen.gba", linkGroup: 0, exportMode: 0, resources: { rom: id }, romName: `rom${id}`, pipeline: [] });
// Every construct-time field of a NES node at its default: buildGraph writes them all, so a document never depends on
// whether a stored config held one, and a re-publish of an unchanged config never moves its node's rebuild key.
const NES_CONSTRUCT = { sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, channelExportMode: "mix" };

test("each system node's settings carry its core config's setting fields, and its params its gain and a NES's expansion volume", () => {
  const sameboy = coreConfig("sameboy", { model: "dmgB", highpass: "removeDcOffset", fastBoot: false, colorCorrection: "lowContrast", dmgPalette: "gbl", lightTemperature: -0.25, linkGroupId: 3 });
  const mesen = coreConfig("mesen", { region: "pal", removeSpriteLimit: true, apuLatencyMs: 2.5, s5bNoise: "chip", expansionVolume: 40, enableFm: false, fifo: "hardware", sdRoot: "/sd" });
  const g = built([
    { ...gb(1), coreConfig: sameboy, gainDb: -6 },
    { ...nes(2), coreConfig: mesen, gainDb: 3 },
    { ...gba(3), coreConfig: mesen, gainDb: -1.5 },
    { ...sms(4), coreConfig: mesen, gainDb: 0 },
  ]);
  // The labels as the role config spells them, the construct-time fields among them (what the node builds its core
  // with) but nothing that is wiring (the link group).
  expect(g.node("sys-1")).toEqual({
    id: "sys-1",
    type: "rp.sameboy.gb",
    settings: { system: 1, rom: "res:1", romName: "rom1", model: "dmgB", fastBoot: false, highpass: "removeDcOffset", colorCorrection: "lowContrast", dmgPalette: "gbl", lightTemperature: -0.25, serialOutCapture: "off" },
    params: { gain: -6 },
  });
  expect(g.node("sys-2")).toEqual({
    id: "sys-2",
    type: "rp.mesen.nes",
    settings: {
      system: 2, rom: "res:2", romName: "rom2", region: "pal", removeSpriteLimit: true, apuLatencyMs: 2.5, s5bNoise: "chip",
      mmc5PhaseReset: "n8", sdRoot: "/sd", fifo: "hardware", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto",
      n8LinkRttUs: 2200, channelExportMode: "mix",
    },
    params: { gain: 3, expansionVolume: 40 },
  });
  expect(g.node("sys-3")).toEqual({ id: "sys-3", type: "rp.mesen.gba", settings: { system: 3, rom: "res:3", romName: "rom3" }, params: { gain: -1.5 } });
  expect(g.node("sys-4")).toEqual({ id: "sys-4", type: "rp.mesen.sms", settings: { system: 4, rom: "res:4", romName: "rom4", enableFm: false }, params: { gain: 0 } });
  // A NES's model export mode and its core config's label name the same thing, and the node carries the label.
  const exportLabel = "individualMono";
  const configured = { ...nes(2, 3), coreConfig: coreConfig("mesen", { channelExportMode: exportLabel }) };
  expect(exportModeOf(configured.type, configured.coreConfig)).toBe(configured.exportMode);
  expect(built([configured]).node("sys-2")?.settings?.channelExportMode).toBe(exportLabel);
});

test("a system with no core config carries its construct-time fields at their defaults, and no knob or param", () => {
  const g = built([gb(1), nes(2), gba(3), sms(4)]);
  for (const id of [1, 2, 3, 4]) expect(g.node(`sys-${id}`)?.params, `sys-${id}`).toBe(undefined);
  expect(g.node("sys-2")?.settings).toEqual({ system: 2, rom: "res:2", romName: "rom2", ...NES_CONSTRUCT });
  expect(g.node("sys-4")?.settings).toEqual({ system: 4, rom: "res:4", romName: "rom4", enableFm: true });
  // A config holding only some live fields gives the node those, and every construct-time field at its default.
  expect(built([{ ...nes(2), coreConfig: { region: "dendy" } }]).node("sys-2")).toEqual({
    id: "sys-2",
    type: "rp.mesen.nes",
    settings: { system: 2, rom: "res:2", romName: "rom2", region: "dendy", ...NES_CONSTRUCT },
  });
});

test("a knob the node would refuse is left out, so one stored config cannot refuse every system's document", () => {
  // A loaded project keeps its core config as written: a hand edit, a newer build's label, an ordinal from before the
  // labels. The planner would refuse the whole document for it.
  const g = built([
    { ...gb(1), coreConfig: { model: "cgbF", highpass: 2, fastBoot: "yes", colorCorrection: null, dmgPalette: "gbl", lightTemperature: 1.5 }, gainDb: Number.NaN },
    { ...nes(2), coreConfig: { region: "PAL", apuLatencyMs: 9, removeSpriteLimit: 1, s5bNoise: "n8", expansionVolume: "40" }, gainDb: 999 },
  ]);
  expect(g.node("sys-1")).toEqual({ id: "sys-1", type: "rp.sameboy.gb", settings: { system: 1, rom: "res:1", romName: "rom1", dmgPalette: "gbl", serialOutCapture: "off" } });
  // A param out of range is sent: the planner clamps it, as it clamps a live edit.
  expect(g.node("sys-2")).toEqual({ id: "sys-2", type: "rp.mesen.nes", settings: { system: 2, rom: "res:2", romName: "rom2", s5bNoise: "n8", ...NES_CONSTRUCT }, params: { gain: 999 } });
});

test("a Game Boy captures its serial out for the mode its lsdj-sync node runs: drive for MI.OUT, listen for master sync, off otherwise", () => {
  const capture = (pipeline: ProtocolRole[]) => built([gb(1, 0, pipeline)]).node("sys-1")?.settings?.serialOutCapture;
  for (const mode of LSDJ_MODE_VALUES)
    expect(capture([LSDJ(mode)]), mode).toBe(mode === "midiOut" ? "drive" : mode === "masterSync" ? "listen" : "off");
  expect(capture([])).toBe("off");
  expect(capture([MGB])).toBe("off");
  // An lsdj-sync the pipeline leaves out (mGB writes serialIn first): its decoder is not placed, so nothing captures.
  const leftOut = protocolPipeline([{ kind: "mgb", config: {} }, { kind: "lsdj-sync", config: { mode: "midiOut" } }]);
  expect(leftOut.map((r) => r.kind)).toEqual(["mgb"]);
  expect(capture(leftOut)).toBe("off");
  // Only a Game Boy has one.
  expect(built([nes(2, 0, [N8])]).node("sys-2")?.settings?.serialOutCapture).toBe(undefined);
  expect(serialOutCaptureFor([LSDJ("midiOut")], "midiMap")).toBe("off");
});

test("a cart the controller drives in MI.MAP captures nothing, whatever its stored mode, and its peers keep their own", () => {
  const captures = (g: ReturnType<typeof built>) => [1, 2].map((id) => g.node(`sys-${id}`)?.settings?.serialOutCapture);
  const systems = [gb(1, 0, [LSDJ("midiOut")]), gb(2, 0, [LSDJ("masterSync")])];
  expect(captures(built(systems))).toEqual(["drive", "listen"]);
  expect(captures(built(systems, "stereo", multi(2), withController()))).toEqual(["off", "listen"]); // forced midiMap
  expect(captures(built(systems, "stereo", multi(2), withController({ systemId: 2 })))).toEqual(["drive", "off"]);
  expect(captures(built(systems, "stereo", multi(2), withController({ systemId: 2, cartSync: "Lsdj" })))).toEqual(["drive", "off"]); // forced off
  // Driving a real Game Boy forces no cart, so none loses its capture.
  expect(captures(built(systems, "stereo", multi(2), withController({ target: "midiOut" })))).toEqual(["drive", "listen"]);
});
