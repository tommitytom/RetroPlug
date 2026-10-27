// buildGraph's wiring, as explicit tables (docs/modular-graph-phase-b2-design.md section 7.2). Each host channel
// must be fed what the routers before B1 added into it, in their order, and systems must run in the order that
// engine stepped them (unlinked by slot, then each link group). These are the B1 translator's own tests,
// carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
// Then the script nodes ahead of the systems and the host's I/O around them (docs/modular-graph-phase-b3b-design.md
// section 4): the MIDI routing, the controller and each role's protocol node, wired as the DSP kernel's ports were.
import { test, expect } from "../../testing/harness";
import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "../../src/graph/buildGraph";
import { protocolPipeline, type ProtocolRole } from "../../src/graph/protocolNodes";
import { controllerSyncOverride, type GraphController } from "../../src/graph/controller";
import { MIDI_ROUTE_NODE_TYPE } from "../../src/script/nodes/midiRoute";
import { CONTROLLER_SESSION_NODE_TYPE } from "../../src/script/nodes/controllerSession";
import { MGB_MIDI_NODE_TYPE } from "../../src/script/nodes/mgbMidi";
import { N8_MIDI_TO_FIFO_NODE_TYPE } from "../../src/script/nodes/n8MidiToFifo";
import { RISA_SYNC_NODE_TYPE } from "../../src/script/nodes/risaSync";
import { SMS_SYNC_NODE_TYPE } from "../../src/script/nodes/smsSync";
import { LSDJ_SYNC_NODE_TYPE } from "../../src/script/nodes/lsdjSync";
import type { AudioRouting, MidiRouting } from "../../src/settingsEnums";

const gb = (id: number, linkGroup = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0, pipeline });
const nes = (id: number, exportMode = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode, pipeline });
const multi = (channels: number): HostShape => ({ kind: "multi", channels });

// The host's I/O and the script nodes ahead of the systems, which every document carries; the B1 tables below are the
// rest, the audio.
const AROUND_THE_AUDIO = new Set(["midi-in", "controller-in", "routing", "controller", "cart", "midi-out", "controller-out"]);
const aroundTheAudio = (id: string): boolean => AROUND_THE_AUDIO.has(id) || id.includes("/");

interface Project {
  midiRouting?: MidiRouting | null;
  controller?: GraphController | null;
}

function built(systems: GraphSystem[], routing: AudioRouting = "stereo", shape: HostShape = multi(2), project: Project = {}) {
  const model: GraphModel = { systems, routing, shape, ...project };
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
  expect(bare.nodeOrder).toEqual(["midi-in", "controller-in", "midi-out", "controller-out", "out"]);
  expect(bare.node("midi-out")?.settings).toEqual({ systems: [] });
  expect(bare.document.wires).toEqual([]);
  expect(bare.buses).toEqual([1, 1]);
  expect(bare.slots).toEqual([]);
  const routed = built([], "stereo", multi(2), { midiRouting: "sendToAll" });
  expect(routed.nodeOrder).toEqual(["midi-in", "controller-in", "routing", "midi-out", "controller-out", "out"]);
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

test("a system node names its core by its system id", () => {
  const g = built([gb(4)]);
  expect(g.node("sys-4")).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { core: 4 } });
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
  const other: GraphSystem = { id: 2, type: "rp.mesen.nes", linkGroup: 1, exportMode: 0, pipeline: [] };
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
const sms = (id: number, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.mesen.sms", linkGroup: 0, exportMode: 0, pipeline });

const CONTROLLER: GraphController = {
  app: "lsdj-midimap", target: "system", systemId: 0, appConfig: { quantise: "bar", follow: true, page: 0 },
  songRowTicks: [], anchor: null, cartSync: null,
};
const withController = (over: Partial<GraphController> = {}): Project => ({ midiRouting: "sendToAll", controller: { ...CONTROLLER, ...over } });

test("each type buildGraph places is the id its node module registers", () => {
  const types = (g: ReturnType<typeof built>) => g.document.nodes.map((n) => n.type);
  const g = built([gb(1, 0, [MGB]), nes(2, 0, [N8, RISA]), sms(3, [SMS]), gb(4, 0, [LSDJ()])], "stereo", multi(2), withController());
  for (const type of [MIDI_ROUTE_NODE_TYPE, CONTROLLER_SESSION_NODE_TYPE, MGB_MIDI_NODE_TYPE, N8_MIDI_TO_FIFO_NODE_TYPE, RISA_SYNC_NODE_TYPE, SMS_SYNC_NODE_TYPE, LSDJ_SYNC_NODE_TYPE])
    expect(types(g).includes(type)).toBe(true);
  expect(protocolPipeline([{ kind: "sameboy", config: {} }, { kind: "lsdj-assets", config: {} }, { kind: "risa", config: {} }])).toEqual([]);
});

test("every script node is placed before the systems: the routing, the controller, then each system's in slot then pipeline order", () => {
  // Slot order, not the sum order a link group gives the systems themselves.
  const g = built([gb(1, 1, [MGB]), nes(2, 0, [N8, RISA]), gb(3, 1, [LSDJ()])], "stereo", multi(2), withController());
  expect(g.nodeOrder.slice(0, 11)).toEqual([
    "midi-in", "controller-in", "routing", "controller", "sys-1/mgb", "sys-2/nes-n8-midi", "sys-2/risa-sync", "sys-3/lsdj-sync",
    "sys-2", "sys-1", "sys-3",
  ]);
  // Each protocol node takes its role's config as its settings.
  expect(g.node("sys-3/lsdj-sync")).toEqual({ id: "sys-3/lsdj-sync", type: LSDJ_SYNC_NODE_TYPE, settings: LSDJ().config });
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

test("MI.OUT comes back from a Game Boy a block later, into its lsdj-sync node and nowhere else", () => {
  const g = built([gb(1, 0, [MGB]), gb(2, 0, [LSDJ("midiOut")]), gb(3)]);
  const back = g.document.wires.filter((w) => w.feedback);
  expect(back).toEqual([{ from: "sys-2.serialOut", to: "sys-2/lsdj-sync.serialOut", feedback: true }]);
});

test("the controller takes the surface, sends it its LEDs, and its target ('0 is the first system') resolved", () => {
  const g = built([gb(7, 0, [LSDJ()]), gb(8, 0, [LSDJ()])], "stereo", multi(2), withController({ anchor: { rows: [5, 5, null, 5], seq: 3 } }));
  expect(g.node("controller")).toEqual({
    id: "controller",
    type: CONTROLLER_SESSION_NODE_TYPE,
    settings: {
      app: "lsdj-midimap", target: "system", systemId: 7, appConfig: { quantise: "bar", follow: true, page: 0 },
      songRowTicks: [], anchorRows: [5, 5, null, 5], anchorSeq: 3,
    },
  });
  expect(g.into("controller.controllerIn")).toEqual(["controller-in.midi"]);
  expect(g.into("controller.controllerConnected")).toEqual(["controller-in.connected"]);
  expect(g.into("controller-out.midi")).toEqual(["controller.controllerOut"]);
  // Its launches join the target cart's routed MIDI, and no other cart's.
  expect(g.into("sys-7/lsdj-sync.launches")).toEqual(["controller.launches"]);
  expect(g.into("sys-8/lsdj-sync.launches")).toEqual([]);
  // A chosen system is taken as it is.
  const chosen = built([gb(7, 0, [LSDJ()]), gb(8, 0, [LSDJ()])], "stereo", multi(2), withController({ systemId: 8 }));
  expect(chosen.into("sys-8/lsdj-sync.launches")).toEqual(["controller.launches"]);
  expect(chosen.into("sys-7/lsdj-sync.launches")).toEqual([]);
  // Without a controller the surface is wired to nothing.
  const without = built([gb(7, 0, [LSDJ()])]);
  expect(without.node("controller")).toBe(undefined);
  expect(without.document.wires.some((w) => w.from.startsWith("controller-in.") || w.to.startsWith("controller-out."))).toBe(false);
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

test("host MIDI out takes the controller's first, then each lsdj-sync node's in slot order, each under its system", () => {
  const g = built([gb(3, 1, [LSDJ("midiOut")]), gb(1, 0, [MGB]), gb(2, 1, [LSDJ("masterSync")])], "stereo", multi(2), withController({ systemId: 2 }));
  expect(g.node("midi-out")?.settings).toEqual({ systems: [2, 3, 2] });
  expect([1, 2, 3].map((k) => g.into(`midi-out.in${k}`))).toEqual([["controller.midiOut"], ["sys-3/lsdj-sync.midiOut"], ["sys-2/lsdj-sync.midiOut"]]);
  // With no system to resolve "the first" to, the controller's is tagged as the kernel tagged it (its -1), and the node
  // takes 0, its none.
  const empty = built([], "stereo", multi(2), withController());
  expect(empty.node("midi-out")?.settings).toEqual({ systems: [0xffffffff] });
  expect(empty.node("controller")?.settings?.systemId).toBe(0);
  // Nothing that sends host MIDI: an input for nothing.
  expect(built([gb(1, 0, [MGB])]).node("midi-out")?.settings).toEqual({ systems: [] });
});

test("the cart mirrors the first NES in slot order, fed what its protocol nodes feed it, and there is no mirror without a NES", () => {
  const g = built([gb(1), nes(9, 0, [N8, RISA]), nes(3, 0, [N8])]);
  expect(g.node("cart")).toEqual({ id: "cart", type: "rp.n8.cart-mirror", settings: { system: 9 } });
  expect(g.into("cart.midi")).toEqual(["sys-9/nes-n8-midi.midi"]);
  expect(g.into("cart.bytes")).toEqual(["sys-9/risa-sync.bytes"]);
  // A NES with no risa-sync sends its cart no bytes; one with no protocol node at all, nothing.
  expect(built([nes(4, 0, [N8])]).into("cart.bytes")).toEqual([]);
  expect(built([nes(4)]).document.wires.some((w) => w.to.startsWith("cart."))).toBe(false);
  expect(built([gb(1), gb(2)]).node("cart")).toBe(undefined);
  expect(built([]).node("cart")).toBe(undefined);
});

// --- the cart's sync mode under a controller ----------------------------------------------------------------
//
// The two settings have to agree and nothing made them. `lsdj-sync` defaults to `midiSync`, while the MI.MAP app's
// launches are NoteOns that only the `midiMap` translator turns into row bytes - so enabling a Launchpad on a fresh
// cart clocked it for a mode it was not in, sent launches nowhere, and left LSDj sitting on "WAIT". Reported from a
// hardware session. The document forces it in that cart's lsdj-sync node alone, so nothing reaches the `.rplg`.

const syncModeOf = (g: ReturnType<typeof built>, id: number): unknown => g.node(`sys-${id}/lsdj-sync`)?.settings?.mode;

test("an enabled controller drives its cart in MI.MAP, whatever the stored mode said", () => {
  const g = built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ cartSync: "MidiMap" }));
  expect(syncModeOf(g, 1)).toBe("midiMap");
  expect(g.node("sys-1/lsdj-sync")?.settings).toEqual({ mode: "midiMap", tempoDivisor: 1, autoStart: false });
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

test("the override leaves the model's own pipeline as it was", () => {
  // The whole reason it happens in the document: turning the controller off restores whatever the user had.
  const systems = [gb(1, 0, [LSDJ()])];
  expect(syncModeOf(built(systems, "stereo", multi(2), withController({ cartSync: "MidiMap" })), 1)).toBe("midiMap");
  expect(systems[0].pipeline[0].config.mode).toBe("midiSync");
  expect(syncModeOf(built(systems), 1)).toBe("midiSync");
});
