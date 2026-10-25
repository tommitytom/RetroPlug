// The graph the stores publish (docs/modular-graph-phase-b2-design.md section 3): one setGraph per edit that
// changes what runs, re-arming the NES tap where the edit builds or drops a core or changes the routing; a
// project load publishing its clear, then placing every system at once, whatever its adopts did; the build
// spec carrying serial-out capture; and DirectGraph keeping the B1 translator's model for callers with no store.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { SystemsStore } from "../../src/systemsStore";
import { DirectGraph } from "../../src/graph/directGraph";
import { buildGraph, type HostShape } from "../../src/graph/buildGraph";
import { toGraphJson } from "../../src/graph/graphDocument";
import { buildAppRegistry, syncDspFromStore } from "../../src/appHost";
import type { DspRuntimeClient } from "../../src/dspRuntime";
import { LsdjSyncMode, type AudioRouting } from "../../src/settingsEnums";
import type { EngineGraph } from "../../src/engineGraph";
import type { ConstructSpec, SetGraphResult } from "../../src/backend";
import { gbRom, lsdjRom, nesRom } from "./fixtures";

function project() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.nes", nesRom());
  be.seed("/roms/lsdj.gb", lsdjRom());
  const p = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  return { be, p, sys: p.systems };
}

// The requests sent since `from`: their slots, and their tap re-arm when they carry one.
const since = (be: MockBackend, from: number) => be.graphRequests.slice(from).map((r) => ({ slots: r.slots, tap: r.tap }));
const linked = (be: MockBackend, to: string): boolean =>
  (JSON.parse(be.graphRequests[be.graphRequests.length - 1].document) as { wires: { to: string }[] }).wires.some((w) => w.to === to);

// Count the publishes a graph is asked for, sent or not: the dedupe would hide one that changes nothing.
function countPublishes(graph: EngineGraph): { n: number } {
  const count = { n: 0 };
  const publish = graph.publish.bind(graph);
  graph.publish = (...args) => {
    count.n++;
    return publish(...args);
  };
  return count;
}

// Record what the engine answered each setGraph from here on.
function answers(be: MockBackend): SetGraphResult[] {
  const out: SetGraphResult[] = [];
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    const result = setGraph(request);
    out.push(result);
    return result;
  };
  return out;
}

// Refuse the next `n` setGraph calls (recorded, as the mock records every request), then answer as the mock does.
function refuseNext(be: MockBackend, n: number): void {
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    if (n-- <= 0) return setGraph(request);
    be.graphRequests.push(request);
    return { ok: false, errors: [{ nodeId: "", port: "", code: "busy", message: "refused for the test" }] };
  };
}

// The document a store's systems build at `routing` and `shape`.
const documentFor = (sys: SystemsStore, routing: AudioRouting, shape: HostShape): string =>
  toGraphJson(buildGraph({ systems: sys.graphSystems(), routing, shape }).document);

// A core the mock still holds (built, parked or placed): its state reads answer.
const alive = (be: MockBackend, id: number): boolean => be.readState(id) !== null;

test("adding or removing a system publishes once, re-arming the tap", () => {
  const { be, sys } = project();
  expect(be.graphRequests.length).toBe(0); // composing a project sends nothing
  const a = sys.addSystem("/roms/a.gb")!;
  expect(since(be, 0)).toEqual([{ slots: [a], tap: {} }]);
  const b = sys.addSystem("/roms/b.gb")!;
  expect(since(be, 1)).toEqual([{ slots: [a, b], tap: {} }]);
  sys.removeSystem(a);
  expect(since(be, 2)).toEqual([{ slots: [b], tap: {} }]);
});

test("a replace takes the slot of the system it replaces", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const b = sys.addSystem("/roms/b.gb")!;
  const n = be.graphRequests.length;
  const c = sys.replaceSystem(a, "/roms/c.nes")!;
  expect(since(be, n)).toEqual([{ slots: [c, b], tap: {} }]);
  expect(be.replacedBy(be.buildCalls[be.buildCalls.length - 1])).toBe(a);
  // The replacing NES was built in the default (mix) mode, and the graph places it as a NES.
  expect(sys.systems()[0].built).toEqual({ type: "rp.mesen.nes", exportMode: 0, linkGroup: 0 });
});

test("a live setting publishes nothing; a link-group change rewires without a re-arm", () => {
  const { be, p, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const b = sys.addSystem("/roms/b.gb")!;
  const n = be.graphRequests.length;
  const publishes = countPublishes(p.graph);
  sys.setGain(a, -6);
  sys.setRoleConfig(a, "sameboy", { highpass: "removeDcOffset" });
  sys.setRoleConfig(a, "sameboy", { model: "dmgB", dmgPalette: "dmg" });
  sys.setRoleConfig(a, "sameboy", { linkGroupId: 0 }); // the group it is already in
  expect(publishes.n).toBe(0); // not even a document for the dedupe to drop
  expect(since(be, n)).toEqual([]);
  sys.setRoleConfig(a, "sameboy", { linkGroupId: 1 }); // a group of one is no cable yet: nothing changes
  expect(publishes.n).toBe(1);
  expect(since(be, n)).toEqual([]);
  sys.setRoleConfig(b, "sameboy", { linkGroupId: 1 });
  expect(since(be, n)).toEqual([{ slots: [a, b] }]);
  expect(linked(be, `sys-${b}.link`)).toBe(true);
});

test("a duplicate is placed after its source, and inheriting the link group cables the pair", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const n = be.graphRequests.length;
  const d = sys.duplicateSystem(a)!;
  expect(since(be, n)).toEqual([{ slots: [a, d], tap: {} }]);
  // Promoting the lone source to group 1 is a group of one (the document it already runs); the child joining
  // it is the cable, sent without a re-arm.
  sys.inheritLinkGroup(d, a);
  expect(since(be, n + 1)).toEqual([{ slots: [a, d] }]);
  expect(linked(be, `sys-${d}.link`)).toBe(true);
  expect(sys.systems().map((e) => e.built.linkGroup)).toEqual([1, 1]);
});

test("a routing change re-arms the tap, even to the mode already set", () => {
  const { be, p, sys } = project();
  const c = sys.addSystem("/roms/c.nes")!;
  const n = be.graphRequests.length;
  p.setAudioRouting("stereo"); // the default: the document is unchanged, and it is sent for the re-arm
  p.setAudioRouting("pinSplit");
  expect(since(be, n)).toEqual([
    { slots: [c], tap: {} },
    { slots: [c], tap: { arm: { id: c, mode: 1 } } },
  ]);
});

test("a new project publishes its clear, then its routing", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const n = be.graphRequests.length;
  p.newProject();
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: [], tap: {} },
  ]);
});

test("a load publishes its clear, then places every system once", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const sent = since(be, n);
  expect(sent.length).toBe(2);
  expect(sent[0]).toEqual({ slots: [], tap: {} });
  expect(sent[1].slots).toEqual(sys.systems().map((e) => e.id));
  expect(sent[1].slots.length).toBe(2);
  expect(sent[1].tap).toEqual({});
});

test("a load where one system does not build publishes once, with the rest", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  sys.addSystem("/roms/c.nes");
  const bytes = p.exportBytes()!;
  const buildCore = be.buildCore.bind(be);
  let builds = 0;
  be.buildCore = (spec, id) => (++builds === 2 ? null : buildCore(spec, id));
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/")).toEqual({ kind: "loaded", systems: 2 });
  const [first, third] = sys.systems().map((e) => e.id);
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: [first, third], tap: {} },
  ]);
});

test("a load that throws partway places what it adopted, and the edits after it plan", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const buildCore = be.buildCore.bind(be);
  let builds = 0;
  be.buildCore = (spec, id) => {
    if (++builds === 2) throw new Error("the build RPC died"); // as an RPC error reply throws
    return buildCore(spec, id);
  };
  // The kernel, as a host wires it: each push records the systems it was given.
  const kernel: number[][] = [];
  const dsp: DspRuntimeClient = {
    compileScript: () => new Uint8Array(0),
    loadKernel: () => true,
    setSystems: (struct) => {
      kernel.push(struct.systems.map((sys) => sys.id));
      return true;
    },
  };
  p.setOnSystemsChange(() => void syncDspFromStore(p, dsp));
  const n = be.graphRequests.length;
  expect(() => p.loadBytes(bytes, "/")).toThrow("the build RPC died");
  be.buildCore = buildCore;
  const adopted = sys.systems().map((e) => e.id);
  expect(adopted.length).toBe(1);
  // The clear, then the system built before the throw, placed with the re-arm a load's own publish carries.
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: adopted, tap: {} },
  ]);
  expect(kernel).toEqual([adopted]); // and pushed to the kernel, which held the previous project's systems
  const answered = answers(be);
  // A render's shape round trip rebuilds that model, so the core stays placed under the store.
  expect(p.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(alive(be, adopted[0])).toBe(true);
  const added = sys.addSystem("/roms/a.gb")!;
  expect(since(be, be.graphRequests.length - 1)).toEqual([{ slots: [adopted[0], added], tap: {} }]);
  expect(answered.length).toBe(3);
  expect(answered.every((r) => r.ok)).toBe(true);
});

test("an adopt whose settings do not parse throws before it builds a core", () => {
  const { be, sys } = project();
  expect(() => sys.adopt({ romPath: "/roms/a.gb", settings: "loud" as never })).toThrow();
  expect(be.buildCalls.length).toBe(0);
  expect(sys.systems().length).toBe(0);
});

test("a load publishes its project's routing, not the one before it", () => {
  const { be, p, sys } = project();
  const shape: HostShape = { kind: "multi", channels: 8 };
  expect(p.graph.setShape(shape)).toBe(true); // the plugin's width, so each pair is its own
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  p.setAudioRouting("twoPerInstance");
  const bytes = p.exportBytes()!;
  p.newProject(); // back to the default routing
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const last = be.graphRequests[be.graphRequests.length - 1];
  expect(last.document).toBe(documentFor(sys, "twoPerInstance", shape));
  expect(last.document === documentFor(sys, "stereo", shape)).toBe(false); // and that is not the default's
});

test("a pinSplit project's lone NES arms the tap in the load's publish", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/c.nes");
  p.setAudioRouting("pinSplit");
  const bytes = p.exportBytes()!;
  p.newProject();
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const [nes] = sys.systems().map((e) => e.id);
  expect(since(be, n)).toEqual([{ slots: [nes], tap: { arm: { id: nes, mode: 1 } } }]); // an empty clear sends nothing
});

test("an adopt outside a load places its core at once", () => {
  const { be, sys } = project();
  const id = sys.adopt({ romPath: "/roms/a.gb" })!;
  expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
});

test("a system in an LSDj MIDI-out mode is built capturing its serial out", () => {
  const { be, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(undefined); // midiSync by default
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1]).toEqual({ id, on: true }); // live
  sys.reset(id); // a rebuild: the new core is armed at the build
  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(true);
});

test("a systems store on its own publishes its systems at the stereo routing", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const id = new SystemsStore(be).addSystem("/roms/a.gb")!;
  expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
});

const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

test("DirectGraph keeps the B1 model: append, replace in place, remove shifting down", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  expect(g.add(MGB, 1) && g.add(MGB, 2) && g.add(MGB, 3)).toBe(true);
  expect(g.replace(1, MGB, 4)).toBe(true);
  expect(g.ids()).toEqual([4, 2, 3]);
  expect(g.remove(2)).toBe(true);
  expect(g.ids()).toEqual([4, 3]);
  expect(since(be, 0).map((r) => r.slots)).toEqual([[1], [1, 2], [1, 2, 3], [4, 2, 3], [4, 3]]);
  // A remove or replace of an id already gone re-arms and changes nothing else.
  const n = be.graphRequests.length;
  expect(g.remove(9)).toBe(true);
  expect(g.replace(9, MGB, 5)).toBe(false);
  expect(since(be, n)).toEqual([
    { slots: [4, 3], tap: {} },
    { slots: [4, 3], tap: {} },
  ]);
  expect(be.buildCalls.some((c) => c.id === 5)).toBe(false);
});

test("DirectGraph rewires a Game Boy's link group as the core is told it", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  g.add(MGB, 2);
  const n = be.graphRequests.length;
  g.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
  g.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
  expect(be.applyRoleCalls.length).toBe(2);
  expect(since(be, n)).toEqual([{ slots: [1, 2] }]);
  expect(linked(be, "sys-2.link")).toBe(true);
  // A config with no link group decodes natively as group 0, so the cable goes with it.
  g.applyRoleConfig(2, "sameboy", { model: 1 });
  expect(linked(be, "sys-2.link")).toBe(false);
});

test("DirectGraph republishes the model a refused edit put back, which frees the core the edit built", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  refuseNext(be, 1);
  const n = be.graphRequests.length;
  expect(g.add(MGB, 2)).toBe(false);
  expect(g.ids()).toEqual([1]);
  // The refused document, then the restored model, carrying the refused re-arm: placing only 1 frees the parked 2.
  expect(since(be, n)).toEqual([
    { slots: [1, 2], tap: {} },
    { slots: [1], tap: {} },
  ]);
  expect(alive(be, 2)).toBe(false);
  // A shape change builds the restored model, not the refused one.
  const m = be.graphRequests.length;
  expect(g.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(since(be, m).map((r) => r.slots)).toEqual([[1], [1]]);
  // And the id builds again.
  expect(g.add(MGB, 2)).toBe(true);
  expect(g.ids()).toEqual([1, 2]);
  expect(alive(be, 2)).toBe(true);
});
