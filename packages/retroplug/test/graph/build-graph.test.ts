// buildGraph's wiring, as explicit tables (docs/modular-graph-phase-b2-design.md section 7.2). Each host channel
// must be fed what the routers before B1 added into it, in their order, and systems must run in the order that
// engine stepped them (unlinked by slot, then each link group). These are the B1 translator's own tests,
// carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
import { test, expect } from "../../testing/harness";
import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "../../src/graph/buildGraph";
import type { AudioRouting } from "../../src/settingsEnums";

const gb = (id: number, linkGroup = 0): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0 });
const nes = (id: number, exportMode = 0): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode });
const multi = (channels: number): HostShape => ({ kind: "multi", channels });

function built(systems: GraphSystem[], routing: AudioRouting = "stereo", shape: HostShape = multi(2)) {
  const model: GraphModel = { systems, routing, shape };
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
  const buses = document.nodes.find((n) => n.id === "out")?.settings?.buses;
  return { document, slots, tapArm, from, lane, nodeOrder, buses };
}

test("the empty project is the kernel and the host's outputs", () => {
  const g = built([]);
  expect(g.nodeOrder).toEqual(["kernel", "out"]);
  expect(g.buses).toEqual([1, 1]);
  expect(g.slots).toEqual([]);
});

test("Stereo sums every system into one pair, in slot order", () => {
  const g = built([gb(1), nes(2), gb(3)]);
  expect(g.nodeOrder).toEqual(["kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.lane(0)).toEqual(["split-1.out1", "split-2.out1", "split-3.out1"]);
  expect(g.lane(1)).toEqual(["split-1.out2", "split-2.out2", "split-3.out2"]);
  expect(built([gb(1), gb(3)]).lane(0)).toEqual(["split-1.out1", "split-3.out1"]);
});

test("a system node names its core by its system id", () => {
  const g = built([gb(4)]);
  expect(g.document.nodes[1]).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { core: 4 } });
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
  expect(g.nodeOrder).toEqual(["kernel", "sys-2", "sys-1", "sys-3", "split-2", "split-1", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.from("sys-3.link")).toBe("sys-1.link");
  expect(g.lane(0)).toEqual(["split-2.out1", "split-1.out1", "split-3.out1"]);
  // Unlinking one of two dissolves the group.
  const apart = built([gb(1, 1), gb(2), gb(3)]);
  expect(apart.nodeOrder).toEqual(["kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(apart.from("sys-3.link")).toBe("");
});

test("only Game Boys link: a link group on any other system is ignored", () => {
  const other: GraphSystem = { id: 2, type: "rp.mesen.nes", linkGroup: 1, exportMode: 0 };
  const g = built([gb(1, 1), other]);
  expect(g.nodeOrder.slice(1, 3)).toEqual(["sys-1", "sys-2"]);
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
  expect(two.nodeOrder[1]).toBe("sys-2");
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
