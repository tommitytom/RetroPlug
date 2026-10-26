// The DSP kernel as a script node (docs/modular-graph-phase-b3a-design.md section 4) in a real Engine with real
// cores. mGB and a BlipToaster NES, each on its own pair of a 4-lane host, play the blocks groups' phrase, rendered
// twice from the same boot: through today's path (the stores' document, whose native rp.dsp.kernel runs the kernel
// stage on the structure they push through setSystems), and through a document built here from that one, the
// kernel node in place of the native kernel with the same structure as its settings, rp.host.midi-in into it and
// its outputs into each system's inputs, as buildGraph is to wire it. The audio is the same, sample for sample.
//
// Here among the parity files because the two renders must start from cores in the same state: the runner powers
// cores on in a fixed one for parity files only (RETROPLUG_DETERMINISTIC).
import { test, expect } from "../../testing/harness";
import { Rig, cycle, even, type Segment } from "./rig";
import { BOOT_MS, NES_CHANNELS, ROMS, frames, mgb, phrase } from "./scenes";
import { KERNEL_NODE_TYPE } from "../../src/script/kernelNode";
import type { KernelStructure } from "../../src/dspKernel";
import type { SetGraphRequest } from "../../src/backend";
import { toGraphJson, type GraphDocument } from "../../src/graph/graphDocument";

const SR = 48000;
const FOUR_LANES = { kind: "multi", channels: 4 } as const;
const rig = new Rig();

// The system inputs the kernel node feeds (design section 3.1).
const SYSTEM_INPUTS = ["serialIn", "midi", "bytes", "buttons"];

/** `request`'s document with the kernel node, set to `structure`, in place of the native kernel, and wired. */
function throughKernelNode(request: SetGraphRequest, structure: KernelStructure): SetGraphRequest {
  const doc = JSON.parse(request.document) as GraphDocument;
  const types = new Map(doc.nodes.map((n) => [n.id, n.type]));
  const settings = {
    project: structure.project ?? [],
    systems: structure.systems.map((s) => ({ ...s, serialOut: types.get(`sys-${s.id}`) === "rp.sameboy.gb" })),
  };
  const at = doc.nodes.findIndex((n) => n.type === "rp.dsp.kernel");
  expect(at, "the stores' document holds the native kernel").toBeGreaterThan(-1);
  doc.nodes.splice(at, 1, { id: "midi-in", type: "rp.host.midi-in" }, { id: "kernel", type: KERNEL_NODE_TYPE, settings });
  doc.wires.push({ from: "midi-in.midi", to: "kernel.midiIn" });
  for (const s of settings.systems) {
    for (const port of SYSTEM_INPUTS) doc.wires.push({ from: `kernel.sys-${s.id}.${port}`, to: `sys-${s.id}.${port}` });
    // MI.OUT's one block of latency, as the engine's gather gives it today.
    if (s.serialOut) doc.wires.push({ from: `sys-${s.id}.serialOut`, to: `kernel.sys-${s.id}.serialOut`, feedback: true });
  }
  return { document: toGraphJson(doc), slots: request.slots };
}

/** Boot the scene, render it, and return its lanes; through the kernel node when `viaNode`. */
function render(split: (n: number) => number[], viaNode: boolean): Float32Array[] {
  rig.reset(SR);
  const { backend, dsp } = rig.session;
  // The stores publish at the 4-lane shape the renders use, so a render sends no document of its own (which would
  // put the stores' document back in place of the one sent here).
  expect(rig.project.graph.setShape(FOUR_LANES)).toBe(true);
  const setGraph = backend.setGraph;
  const setSystems = dsp.setSystems;
  let request: SetGraphRequest | null = null;
  let structure: KernelStructure | null = null;
  backend.setGraph = (r) => ((request = r), setGraph(r));
  dsp.setSystems = (s) => ((structure = s), setSystems(s));
  try {
    mgb(rig);
    rig.add(ROMS.bliptoaster);
    rig.project.setAudioRouting("twoPerInstance");
  } finally {
    backend.setGraph = setGraph;
    dsp.setSystems = setSystems;
  }
  expect(request !== null && structure !== null, "the stores published a document and a structure").toBe(true);
  if (viaNode) {
    const r = setGraph(throughKernelNode(request!, structure!));
    expect(r.errors.map((e) => `${e.code} at ${e.nodeId}.${e.port}: ${e.message}`)).toEqual([]);
  }
  // sendToAll: both systems hear the phrase, mGB on its voices 0..3 and the NES on its channels 0..3.
  const segments: Segment[] = [
    { blocks: split(frames(SR, BOOT_MS)) },
    { blocks: split(frames(SR, 600)), midi: phrase(SR, NES_CHANNELS) },
  ];
  const parts = segments.map((seg) => {
    const r = rig.render(4, seg);
    expect(r.midiOut).toEqual([]);
    return r.lanes;
  });
  return [0, 1, 2, 3].map((l) => {
    const lane = new Float32Array(parts.reduce((n, p) => n + p[l].length, 0));
    let at = 0;
    for (const p of parts) {
      lane.set(p[l], at);
      at += p[l].length;
    }
    return lane;
  });
}

function rms(a: Float32Array, from: number): number {
  let s = 0;
  for (let i = from; i < a.length; i++) s += a[i] * a[i];
  return Math.sqrt(s / Math.max(1, a.length - from));
}

for (const [name, split] of [
  ["512-frame blocks", (n: number) => even(n, 512)],
  ["FL-style ragged blocks, 1-frame ones among them", (n: number) => cycle(n, [33, 1, 222, 1, 7, 4096, 2])],
] as const) {
  test(`mGB and a NES render the same through the kernel node as through today's kernel stage, in ${name}`, () => {
    const today = render(split, false);
    const node = render(split, true);
    for (let l = 0; l < 4; l++) {
      expect(node[l].length, `lane ${l}`).toBe(today[l].length);
      let first = -1;
      for (let i = 0; i < today[l].length && first < 0; i++) if (!Object.is(today[l][i], node[l][i])) first = i;
      expect(first, `lane ${l}: the first frame that differs`).toBe(-1);
      // Each system played the phrase: its pair sounds once the MIDI arrives, so the two did not agree on silence.
      expect(rms(today[l], frames(SR, BOOT_MS)), `lane ${l} sounds`).toBeGreaterThan(0.001);
    }
  });
}
