// The engine's graph as TS builds it (docs/modular-graph-phase-b2-design.md section 2.1): the systems in slot
// order, the project's audio routing and the host's output shape, turned into the document setGraph plans.
// Pure, and held to the B1 native translator's output by test/graph/golden/build-graph.json, so every node
// id, the document order (which is the plan order on ties) and every per-lane sum is the translator's.
import type { AudioRouting } from "../settingsEnums";
import type { GraphDocument, NodeDoc, WireDoc } from "./graphDocument";

export type SystemNodeType = "rp.sameboy.gb" | "rp.mesen.nes" | "rp.mesen.gba" | "rp.mesen.sms";

/** The output shape a document is built for: the host's own outputs routed by the project's mode (multi),
 *  each system on its own pair (perSystem), or one system's streams on a pair each (perChannel). A multi
 *  shape's `channels` is a positive integer: every placement is modulo it. */
export type HostShape = { kind: "multi"; channels: number } | { kind: "perSystem" } | { kind: "perChannel" };

export interface GraphSystem {
  /** The TS system id. */
  id: number;
  /** The node type its core is placed as, as buildCore reported it. */
  type: SystemNodeType;
  /** Game Boys only (ignored for any other type); 0 = unlinked. */
  linkGroup: number;
  /** NES only: the channel export mode its core was built with. */
  exportMode: number;
}

export interface GraphModel {
  /** Slot order. */
  systems: readonly GraphSystem[];
  routing: AudioRouting;
  shape: HostShape;
}

/** The NES tap the engine arms for a lone NES under a split routing: its pins (1) or its five channels (3).
 *  (The engine skips the arm when the core is already in that mode.) */
export interface TapArm {
  id: number;
  mode: 1 | 3;
}

export interface BuiltGraph {
  document: GraphDocument;
  /** System ids in slot order: the engine's system table, which its tail still walks while the kernel is sealed. */
  slots: number[];
  /** What the engine's SyncTap arms on an edit that re-arms, or null for nothing (a restore only). */
  tapArm: TapArm | null;
}

/** rp.core.add's input limit (CoreNodes.cpp): a longer sum chains adds, continuing the same left-to-right sum. */
const ADD_INPUTS = 16;

interface Stream {
  port: string;
  stereo: boolean;
}

const sysNode = (id: number): string => `sys-${id}`;

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

  nodes.push({ id: "kernel", type: "rp.dsp.kernel" });
  const groups = linkGroups(systems);
  const order = sumOrder(systems, groups);
  for (const i of order) nodes.push({ id: sysNode(systems[i].id), type: systems[i].type, settings: { core: systems[i].id } });
  for (const g of groups)
    for (let k = 1; k < g.length; ++k) wire(`${sysNode(systems[g[k - 1]].id)}.link`, `${sysNode(systems[g[k]].id)}.link`);

  const buses: number[] = [];
  if (shape.kind === "perSystem") {
    // Slot i's mix on channels 2i and 2i + 1.
    systems.forEach((s, i) => {
      wire(`${sysNode(s.id)}.mix`, outPort(2 * i, 2));
      buses.push(2);
    });
  } else if (shape.kind === "perChannel") {
    // One system's stream k on channels 2k and 2k + 1; a mono stream fills only 2k.
    if (systems.length === 1) {
      const s = systems[0];
      streamsOf(s.type, nesMode(tapArm, s)).forEach((stream, k) => {
        if (stream.stereo) {
          wire(`${sysNode(s.id)}.${stream.port}`, outPort(2 * k, 2));
          buses.push(2);
        } else {
          wire(`${sysNode(s.id)}.${stream.port}`, outPort(2 * k, 1));
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
        const from = `${sysNode(s.id)}.${stream.port}`;
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
        const [lo, ro] = halves(`${sysNode(s.id)}.mix`, `split-${s.id}`);
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
