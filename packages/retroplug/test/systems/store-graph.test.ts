// The graph the stores publish (docs/modular-graph-phase-b2-design.md section 3): one setGraph per edit that
// changes what runs, re-arming the NES tap where the edit builds or drops a core or changes the routing; a
// project load publishing its clear, then placing every system at once, whatever its adopts did; the build
// spec carrying serial-out capture; and DirectGraph keeping the B1 translator's model for callers with no store.
// Each document places the script nodes that feed the systems (docs/modular-graph-phase-b3b-design.md section 4): the
// MIDI routing, the controller and a protocol node for each role that has one, so an edit to such a role, the MIDI
// routing or the controller publishes too, and nothing else does.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { SystemsStore } from "../../src/systemsStore";
import { DirectGraph } from "../../src/graph/directGraph";
import { buildGraph, type HostShape } from "../../src/graph/buildGraph";
import { toGraphJson } from "../../src/graph/graphDocument";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { buildAppRegistry } from "../../src/appHost";
import { LsdjSyncMode, MidiRouting, type AudioRouting } from "../../src/settingsEnums";
import type { RoleInstance } from "../../src/systemRoles";
import type { EngineGraph } from "../../src/engineGraph";
import type { ConstructSpec, SetGraphResult } from "../../src/backend";
import { z, clampedInt } from "../../src/configSchema";
import { blipToasterRom, gbRom, lsdjRom, nesRom } from "./fixtures";

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

// The project stage DirectGraph takes for the MIDI routing a project has by default: every message to every system.
const SEND_TO_ALL: RoleInstance = { kind: "midi-routing", config: { mode: "sendToAll" } };

// `v` as a document carries it: canonical JSON, its keys sorted.
const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));

// Whether the last document sent wires `from` into `to` (not over a feedback wire).
const wired = (be: MockBackend, from: string, to: string): boolean =>
  be.lastDocument()!.wires.some((w) => w.from === from && w.to === to && !w.feedback);

// The document a store's systems build at `routing` and `shape`, with the default MIDI routing and no controller.
const documentFor = (sys: SystemsStore, routing: AudioRouting, shape: HostShape): string =>
  toGraphJson(buildGraph({ systems: sys.graphSystems(), routing, shape, midiRouting: "sendToAll" }).document);

// The systems the routing node lists, in slot order, in the last document sent.
const routed = (be: MockBackend): unknown => be.graphNode("routing")?.settings?.systems;

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
  const n = be.graphRequests.length;
  let notified = 0;
  p.setOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the build RPC died");
  be.buildCore = buildCore;
  const adopted = sys.systems().map((e) => e.id);
  expect(adopted.length).toBe(1);
  // The structure's observers hear of it (the UI's tiles, the plugin's watched ROMs): the clear and the adopts are
  // quiet, so this is the one notification of the load.
  expect(notified).toBe(1);
  // The clear, then the system built before the throw, placed with the re-arm a load's own publish carries.
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: adopted, tap: {} },
  ]);
  // And the document routes to it (the previous project's systems are gone with it).
  expect(routed(be)).toEqual(adopted);
  const answered = answers(be);
  // A render's shape round trip rebuilds that model, so the core stays placed under the store.
  expect(p.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(alive(be, adopted[0])).toBe(true);
  const added = sys.addSystem("/roms/a.gb")!;
  expect(since(be, be.graphRequests.length - 1)).toEqual([{ slots: [adopted[0], added], tap: {} }]);
  expect(answered.length).toBe(3);
  expect(answered.every((r) => r.ok)).toBe(true);
});

test("a load whose final publish throws still tells the observers once", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const buildCore = be.buildCore.bind(be);
  const setGraph = be.setGraph.bind(be);
  let builds = 0;
  be.buildCore = (spec, id) => {
    builds++;
    return buildCore(spec, id);
  };
  // The load's clear publishes before any build, and its final publish after both: that one dies, as an RPC would.
  be.setGraph = (...args) => {
    if (builds === 2) throw new Error("the setGraph RPC died");
    return setGraph(...args);
  };
  let notified = 0;
  p.setOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the setGraph RPC died");
  be.buildCore = buildCore;
  be.setGraph = setGraph;
  expect(sys.systems().length).toBe(2);
  expect(notified).toBe(1);
});

test("a load whose clear's publish throws still tells the observers once", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const setGraph = be.setGraph.bind(be);
  let calls = 0;
  // The clear drops the previous project's systems and publishes the emptied graph, which dies, as an RPC would.
  be.setGraph = (...args) => {
    if (++calls === 1) throw new Error("the setGraph RPC died");
    return setGraph(...args);
  };
  let notified = 0;
  p.setOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the setGraph RPC died");
  be.setGraph = setGraph;
  expect(sys.systems().length).toBe(0);
  expect(notified).toBe(1);
  // The failed load placed what it listed, nothing: the clear's document, sent again.
  expect(routed(be)).toEqual([]);
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

test("a system in an LSDj MIDI-out mode is built capturing its serial out: MI.OUT drives, master sync listens", () => {
  const { be, sys } = project();
  let id = sys.addSystem("/roms/lsdj.gb")!;
  const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
  const lastLive = () => be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1];
  expect(lastBuild()).toBe(undefined); // midiSync by default
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(lastLive()).toEqual({ id, mode: "drive" }); // live
  id = sys.reset(id)!; // a rebuild, under a new id: the new core is armed at the build
  expect(lastBuild()).toBe("drive");
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
  expect(lastLive()).toEqual({ id, mode: "listen" });
  id = sys.reset(id)!;
  expect(lastBuild()).toBe("listen");
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
  expect(lastLive()).toEqual({ id, mode: null });
});

test("a systems store on its own publishes its systems at the stereo routing", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const id = new SystemsStore(be).addSystem("/roms/a.gb")!;
  expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
});

test("each document places the routing and a protocol node for each role that has one, in slot order", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/lsdj.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  expect(be.graphNode("routing")?.settings).toEqual({ mode: "sendToAll", systems: [a, c] });
  expect(be.graphNode("controller")).toBe(undefined); // none enabled
  // Only the roles with a protocol node are placed: the core's own config role and the feature roles with no DSP
  // behaviour (a ROM's asset overrides) stay out of the document, so their edits leave it as it was.
  const roles = sys.view().map((v) => v.roles.map((r) => r.kind));
  expect(roles[0].includes("sameboy")).toBe(true);
  expect(roles[0].includes("lsdj-assets")).toBe(true);
  expect(be.protocolNodes(a)).toEqual([`sys-${a}/lsdj-sync`]);
  expect(be.protocolNodes(c)).toEqual([`sys-${c}/nes-n8-midi`]);
  // The Game Boy's serial out comes back into its lsdj-sync node, a block late.
  expect(be.lastDocument()!.wires.filter((w) => w.feedback)).toEqual(
    canon([{ from: `sys-${a}.serialOut`, to: `sys-${a}/lsdj-sync.serialOut`, feedback: true }]),
  );
});

test("an edit to a role with a protocol node publishes its settings, without a re-arm; the same edit again sends nothing", () => {
  const { be, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  const n = be.graphRequests.length;
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(since(be, n)).toEqual([{ slots: [id] }]);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)).toEqual(
    canon({ id: `sys-${id}/lsdj-sync`, type: "rp.lsdj.sync", settings: { mode: LsdjSyncMode.MidiOut, tempoDivisor: 1, autoStart: false } }),
  );
  // The capture arm is the command it always was, sent before the plan that hands the node the new mode.
  expect(be.log.lastIndexOf("setSerialOutCapture")).toBeLessThan(be.log.lastIndexOf("setGraph"));
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(since(be, n).length).toBe(1);
});

test("a controller session is a node of the document, with the sync mode its cart needs", () => {
  const { be, p, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  const n = be.graphRequests.length;
  p.setController({ enabled: true, app: "lsdj-midimap", target: "system", systemId: id });
  expect(since(be, n)).toEqual([{ slots: [id] }]);
  // Its app config parsed by the app's schema on this side, every default filled; its launches into the cart.
  expect(be.graphNode("controller")?.settings).toEqual(
    canon({
      app: "lsdj-midimap", target: "system", systemId: id, appConfig: { quantise: "bar", follow: true, page: 0 },
      songRowTicks: [], anchorRows: [], anchorSeq: 0,
    }),
  );
  expect(wired(be, "controller.launches", `sys-${id}/lsdj-sync.launches`)).toBe(true);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe("midiMap"); // forced in the document, never saved
  expect(sys.view()[0].roles.find((r) => r.kind === "lsdj-sync")?.config.mode).toBe(LsdjSyncMode.MidiSync);
  p.setController({ enabled: false });
  expect(be.graphNode("controller")).toBe(undefined);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe(LsdjSyncMode.MidiSync);
});

test("a load hands the document its project's MIDI routing and controller in the one publish that places its systems", () => {
  // The settings are assigned after the adopts and before that publish. The plugin's setState is a load, so a
  // publish that read the settings before them would route by the previous project's until the next edit.
  const { be, p, sys } = project();
  sys.addSystem("/roms/lsdj.gb");
  p.setMidiRouting(MidiRouting.MidiChannelToInstance);
  p.setController({ enabled: true, app: "lsdj-midimap", target: "system", systemId: 0 });
  const bytes = p.exportBytes()!;
  p.newProject();
  expect(be.graphNode("routing")?.settings).toEqual({ mode: "sendToAll", systems: [] });
  expect(be.graphNode("controller")).toBe(undefined);
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const [id] = sys.systems().map((e) => e.id);
  expect(since(be, n)).toEqual([{ slots: [id], tap: {} }]); // an empty clear sends nothing
  expect(be.graphNode("routing")?.settings).toEqual({ mode: MidiRouting.MidiChannelToInstance, systems: [id] });
  expect(be.graphNode("controller")?.settings?.systemId).toBe(id); // "the first system", resolved
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe("midiMap"); // the controller's cart, in the mode it needs
});

test("an asset-role edit publishes nothing: no asset role has a protocol node, so the document carries none", () => {
  const { be, p, sys } = project();
  be.seed("/roms/bt.nes", blipToasterRom());
  const lsdj = sys.addSystem("/roms/lsdj.gb")!;
  const bt = sys.addSystem("/roms/bt.nes")!;
  const roles = sys.view().map((v) => v.roles.map((r) => r.kind));
  expect(roles[0].includes("lsdj-assets")).toBe(true);
  expect(roles[1].includes("bliptoaster-assets")).toBe(true);
  const publishes = countPublishes(p.graph);
  const n = be.graphRequests.length;
  const overrides = [{ type: "kit", slot: 0, name: "NEWK", path: "/kits/new.kit" }];
  expect(sys.setRoleConfig(lsdj, "lsdj-assets", { overrides })).toBe(true);
  expect(sys.setRoleConfig(bt, "bliptoaster-assets", { settings: { theme: 7 } })).toBe(true);
  expect(publishes.n).toBe(0); // not even a document for the dedupe to drop
  expect(since(be, n)).toEqual([]);
  // The edit is kept on the system, for the reload that applies it.
  expect(sys.view()[0].roles.find((r) => r.kind === "lsdj-assets")?.config.overrides).toEqual(overrides);
});

test("which roles are placed is the protocol-node table's: a registered role with none is not, and its edit publishes nothing", () => {
  // The node types are code in the script bundle, so a role registered at runtime has no node to be placed as,
  // whatever its RoleType says (graph/protocolNodes.ts).
  const registry = buildAppRegistry();
  registry.registerRole({ kind: "probe", category: "feature", schema: z.object({ level: clampedInt(0, 10, 1) }), dsp: () => {} });
  registry.registerRomProvider(({ platform }) => (platform === "gb" ? [{ kind: "probe", config: {} }] : []));
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const p = new ProjectStore(be, new RecentStore(be), registry);
  const id = p.systems.addSystem("/roms/a.gb")!;
  expect(p.systems.view()[0].roles.map((r) => r.kind).includes("probe")).toBe(true);
  expect(be.protocolNodes(id)).toEqual([]);
  const n = be.graphRequests.length;
  expect(p.systems.setRoleConfig(id, "probe", { level: 3 })).toBe(true);
  expect(since(be, n)).toEqual([]);
});

test("a systems store without a registry places the same protocol nodes, and an edit to one publishes", () => {
  // With no registry the store parses no config, so the node gets the role's config as it was stored; which roles
  // are placed does not depend on a registry at all.
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const sys = new SystemsStore(be);
  const id = sys.adopt({
    romPath: "/roms/a.gb",
    roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync } }],
  })!;
  expect(be.protocolNodes(id)).toEqual([`sys-${id}/lsdj-sync`]);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings).toEqual({ mode: LsdjSyncMode.MidiSync });
  // A store on its own routes no MIDI and runs no controller.
  expect(be.graphNode("routing")).toBe(undefined);
  const n = be.graphRequests.length;
  expect(sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut })).toBe(true);
  expect(since(be, n)).toEqual([{ slots: [id] }]);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings).toEqual({ mode: LsdjSyncMode.MidiOut });
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

test("DirectGraph sets each system's pipeline and the project stages, each a publish without a re-arm", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  g.add(MGB, 2);
  // No roles and no stages: no protocol node, no routing and no controller.
  expect(be.lastDocument()!.nodes.map((n) => n.id).slice(0, 4)).toEqual(["midi-in", "controller-in", "sys-1", "sys-2"]);
  const n = be.graphRequests.length;
  const mgb: RoleInstance = { kind: "mgb", config: {} };
  expect(g.setPipeline(2, [{ kind: "sameboy", config: {} }, mgb])).toBe(true); // a role with no node is left out
  expect(g.setProjectStages([SEND_TO_ALL])).toBe(true);
  expect(g.setPipeline(9, [mgb])).toBe(false); // not placed here
  expect(since(be, n)).toEqual([{ slots: [1, 2] }, { slots: [1, 2] }]);
  expect(be.graphNode("routing")?.settings).toEqual({ mode: "sendToAll", systems: [1, 2] });
  expect([be.protocolNodes(1), be.protocolNodes(2)]).toEqual([[], ["sys-2/mgb"]]);
  expect(wired(be, "routing.sys-2", "sys-2/mgb.midi")).toBe(true);
  // A replace is a new system, with no roles; the other keeps its own.
  g.replace(2, MGB, 3);
  g.setPipeline(1, [mgb]);
  expect([be.protocolNodes(1), be.protocolNodes(3)]).toEqual([["sys-1/mgb"], []]);
});

test("DirectGraph takes a launchpad stage as the project's controller, its app config parsed by the app's schema", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  const launchpad: RoleInstance = { kind: "launchpad", config: { app: "lsdj-midimap", target: "midiOut", appConfig: { quantise: "beat" } } };
  expect(g.setProjectStages([SEND_TO_ALL, launchpad])).toBe(true);
  expect(be.graphNode("controller")?.settings).toEqual(
    canon({
      app: "lsdj-midimap", target: "midiOut", systemId: 1, appConfig: { quantise: "beat", follow: true, page: 0 },
      songRowTicks: [], anchorRows: [], anchorSeq: 0,
    }),
  );
  // Bound for a real Game Boy, its launches leave the host, under the system it resolved.
  expect(be.graphNode("midi-out")?.settings).toEqual({ systems: [1] });
  expect(wired(be, "controller.midiOut", "midi-out.in1")).toBe(true);
  expect(g.setController(null)).toBe(true);
  expect(be.graphNode("controller")).toBe(undefined);
  expect(be.graphNode("routing") !== undefined).toBe(true); // the routing stays
});

test("DirectGraph puts the structure back when its document is refused", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  const before = be.lastDocument();
  refuseNext(be, 1);
  expect(g.setPipeline(1, [{ kind: "mgb", config: {} }])).toBe(false);
  refuseNext(be, 1);
  expect(g.setProjectStages([SEND_TO_ALL])).toBe(false);
  // The restored model is what a shape change builds.
  expect(g.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(be.lastDocument()).toEqual(before);
  expect(be.protocolNodes(1)).toEqual([]);
  expect(be.graphNode("routing")).toBe(undefined);
});

test("buildCore refuses id 0 and builds nothing, as native does", () => {
  const { be } = project();
  const spec = { romPath: "/roms/a.gb" } as ConstructSpec;
  expect(be.buildCore(spec, 0)).toBe(null);
  expect(be.setGraph({ document: "{}", slots: [0], tap: {} } as never).ok).toBe(false); // no core under 0
  expect(be.buildCore(spec, 1) != null).toBeTruthy(); // the same spec builds under a real id
});
