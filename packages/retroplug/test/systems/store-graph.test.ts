// The graph the stores publish (docs/modular-graph-phase-b2-design.md section 3): one setGraph per edit that
// changes what runs, re-arming the NES tap where the edit builds or drops a core or changes the routing; a
// project load publishing its clear, then placing every system at once, whatever its adopts did; the build
// spec carrying serial-out capture; and DirectGraph keeping the B1 translator's model for callers with no store.
// Each document places the script nodes that feed the systems (docs/modular-graph-phase-b3b-design.md section 4): the
// MIDI routing, the controller and a protocol node for each role that has one, so an edit to such a role, the MIDI
// routing or the controller publishes too. And each system node carries its knobs
// (docs/modular-graph-phase-c2-design.md section 5): an edit of a setting (a core config's setting field, an LSDj's
// mode and with it its Game Boy's capture) publishes, an edit of a param (a gain, a NES's expansion volume) is set live
// with no publish, and nothing else publishes.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { SystemsStore } from "../../src/systemsStore";
import { DirectGraph } from "../../src/graph/directGraph";
import { buildGraph, type HostShape } from "../../src/graph/buildGraph";
import { toGraphJson, type GraphDocument } from "@retroplug/core/graphDocument";
import { canonicalJson } from "@retroplug/core/canonicalJson";
import { buildAppRegistry } from "../../src/appHost";
import { MidiRouting } from "@retroplug/core/midiRouting";
import { type AudioRouting } from "../../src/settingsEnums";
import { LsdjSyncMode } from "@retroplug/lsdj/enums";
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { EngineGraph } from "../../src/engineGraph";
import type { ConstructSpec, SetGraphResult } from "../../src/backend";
import { z } from "zod";
import { clampedInt } from "@retroplug/core/configFields";
import type { DefinedNode } from "@retroplug/core/defineNode";
import { ROLE_NODES, type LeftOutRole } from "../../src/graph/protocolNodes";
import { SCRIPT_NODES_GLOBAL } from "@retroplug/core/register";
import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
// The production bundle's entry point: importing it registers its node types on SCRIPT_NODES_GLOBAL, as it does in each
// of the script host's contexts.
import "../../src/script/bundle";
import { warned } from "../_warnings";
import { blipToasterRom, gbRom, lsdjRom, nesRom, smsggdjRom } from "./fixtures";

/** The project section the Launchpad pack contributes: a case sets it through the store, as the menu does. */
const CONTROLLER_SECTION = "controller";

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

// `v` as a document carries it: canonical JSON, its keys sorted.
const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));

// A system node's settings beyond the { system, rom, romName } every one carries (the store's puts number its
// resources): the knobs an edit or a load sets, which is what these cases are about. Throws if `id` is not placed.
const knobs = (be: MockBackend, id: number): Record<string, unknown> => {
  const { system, rom, romName, ...rest } = (be.graphNode(`sys-${id}`)?.settings ?? {}) as Record<string, unknown>;
  if (system !== id || typeof rom !== "string" || typeof romName !== "string") throw new Error(`sys-${id} is not placed`);
  return rest;
};

// Every construct-time field of a NES node at its default: buildGraph writes them all, so the document never depends
// on whether the stored config held one.
const NES_CONSTRUCT = {
  sdRoot: "",
  fifo: "instant",
  fifoDepth: 0,
  fifoBytesPerSecond: 0,
  n8Link: "auto",
  n8LinkRttUs: 2200,
  channelExportMode: "mix",
};

// The nodes a role of `kind` on Game Boy `id` is placed as, in document order: lsdj-sync's two (its rp.lsdj.sync, and
// its serial-out decoder after the systems), any other's one.
const roleNodes = (id: number, kind: string): string[] =>
  kind === "lsdj-sync" ? [`sys-${id}/lsdj-sync`, `sys-${id}/lsdj-sync/out`] : [`sys-${id}/${kind}`];

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
  expect(be.replacedBy({ id: c })).toBe(a);
  // The replacing NES boots in the default (mix) mode, and the graph places it as a NES.
  expect(be.bootOf(c)?.type).toBe("rp.mesen.nes");
  expect(be.bootOf(c)?.settings.channelExportMode).toBe("mix");
});

test("a gain or expansion-volume edit sets its param live and publishes nothing", () => {
  const { be, p, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  const n = be.graphRequests.length;
  const publishes = countPublishes(p.graph);
  expect(sys.setGain(a, -6)).toBe(true);
  expect(sys.setGain(c, -500)).toBe(true); // clamped
  expect(sys.setRoleConfig(c, "mesen", { expansionVolume: 40 })).toBe(true);
  expect(sys.setRoleConfig(c, "mesen", { expansionVolume: 40 })).toBe(true); // moves nothing: sets nothing
  expect(publishes.n).toBe(0); // not even a document for the dedupe to drop
  expect(since(be, n)).toEqual([]);
  expect(be.paramCalls).toEqual([
    { nodeId: `sys-${a}`, paramId: "gain", value: -6 },
    { nodeId: `sys-${c}`, paramId: "gain", value: -90 },
    // A NES that drives a cart carries the knob on the cart's node too: a live edit publishes no document, so this
    // is the only thing that moves the real console's master volume.
    { nodeId: `sys-${c}`, paramId: "expansionVolume", value: 40 },
    { nodeId: `cart-${c}`, paramId: "expansionVolume", value: 40 },
  ]);
  // The next document, whatever publishes it, carries the values set live.
  p.setMidiRouting(MidiRouting.OneChannelPerInstance);
  expect(be.graphNode(`sys-${a}`)?.params).toEqual(canon({ gain: -6 }));
  expect(be.graphNode(`sys-${c}`)?.params).toEqual(canon({ gain: -90, expansionVolume: 40 }));
  // A render's shape republish too: it builds from the graph's model, which the live edit kept in step, so it does
  // not put back the values the last document carried.
  expect(p.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(be.graphNode(`sys-${a}`)?.params).toEqual(canon({ gain: -6 }));
  expect(be.graphNode(`sys-${c}`)?.params).toEqual(canon({ gain: -90, expansionVolume: 40 }));
});

test("driving a real cart is placement, not a knob: its edit publishes the document and moves the cart node", () => {
  const { be, p, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  // A NES drives the cart by default: buildGraph places its rp.n8.nes, fed from the same protocol nodes its core is.
  expect(be.graphNode(`cart-${c}`)?.type).toBe("rp.n8.nes");
  const n = be.graphRequests.length;
  const publishes = countPublishes(p.graph);
  expect(sys.setRoleConfig(c, "mesen", { driveRealCart: false })).toBe(true);
  // A document of its own, not a live param: nothing is set on a node, the graph is republished.
  expect(since(be, n).length).toBe(1);
  expect(publishes.n).toBe(1);
  expect(be.graphNode(`cart-${c}`)).toBe(undefined);
  expect(be.lastDocument()!.wires.some((w) => w.to.startsWith(`cart-${c}`))).toBe(false);
  // The emulated node stays and keeps its own wiring (a "real cart only" system is later work).
  expect(be.graphNode(`sys-${c}`)?.type).toBe("rp.mesen.nes");
  // An expansion-volume edit now reaches only the emulated node: no cart node is placed, so none is set (the shared
  // predicate gates both the placement and the live copy).
  const calls = be.paramCalls.length;
  expect(sys.setRoleConfig(c, "mesen", { expansionVolume: 30 })).toBe(true);
  expect(be.paramCalls.slice(calls)).toEqual([{ nodeId: `sys-${c}`, paramId: "expansionVolume", value: 30 }]);
  // Back on: the node returns, and nothing is set live by either edit.
  expect(sys.setRoleConfig(c, "mesen", { driveRealCart: true })).toBe(true);
  expect(be.graphNode(`cart-${c}`)?.type).toBe("rp.n8.nes");
  expect(a).toBeGreaterThan(0);
});

test("a setting edit publishes its system node's settings, without a re-arm; a link-group change rewires", () => {
  const { be, p, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const b = sys.addSystem("/roms/b.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  const n = be.graphRequests.length;
  const publishes = countPublishes(p.graph);
  sys.setRoleConfig(a, "sameboy", { highpass: "removeDcOffset" });
  expect(since(be, n)).toEqual([{ slots: [a, b, c] }]);
  expect(be.graphNode(`sys-${a}`)?.settings?.highpass).toBe("removeDcOffset");
  sys.setRoleConfig(a, "sameboy", { model: "dmgB", dmgPalette: "dmg" });
  expect(knobs(be, a)).toEqual(
    canon({ model: "dmgB", fastBoot: true, highpass: "removeDcOffset", colorCorrection: "disabled", dmgPalette: "dmg", lightTemperature: 0, serialOutCapture: "off" }),
  );
  sys.setRoleConfig(c, "mesen", { region: "pal", expansionVolume: 25 }); // the document carries the param too
  expect(be.graphNode(`sys-${c}`)?.settings?.region).toBe("pal");
  expect(be.graphNode(`sys-${c}`)?.params).toEqual(canon({ gain: 0, expansionVolume: 25 }));
  expect(since(be, n).length).toBe(3);
  expect(be.paramCalls).toEqual([]);
  // An edit that moves no setting publishes nothing: the same values, the group it is already in.
  sys.setRoleConfig(a, "sameboy", { model: "dmgB" });
  sys.setRoleConfig(a, "sameboy", { linkGroupId: 0 });
  expect(publishes.n).toBe(3);
  sys.setRoleConfig(a, "sameboy", { linkGroupId: 1 }); // a group of one is no cable yet: the document is unchanged
  expect(publishes.n).toBe(4);
  expect(since(be, n).length).toBe(3);
  sys.setRoleConfig(b, "sameboy", { linkGroupId: 1 });
  expect(since(be, n)[3]).toEqual({ slots: [a, b, c] });
  expect(linked(be, `sys-${b}.link`)).toBe(true);
});

test("a knob edit reaches the engine as a document or a param, and reload-on-ROM-change not at all", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/lsdj.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  const from = be.log.length;
  sys.setGain(a, -3);
  sys.setReloadOnRomChange(a, true);
  sys.setRoleConfig(a, "sameboy", { model: "sgb", lightTemperature: 0.5 });
  sys.setRoleConfig(a, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
  sys.setRoleConfig(c, "mesen", { region: "ntsc", expansionVolume: 150, removeSpriteLimit: true });
  sys.setRoleConfig(c, "mesen", { expansionVolume: 120 });
  // The gain and the last expansion volume set live (the NES drives a cart, so its knob reaches both nodes), every
  // other edit a document, in that order: nothing else reaches the backend.
  expect(be.log.slice(from)).toEqual(["setParam", "setGraph", "setGraph", "setGraph", "setParam", "setParam"]);
  // reload-on-ROM-change is TS's alone (the file watcher): it reaches no backend at all.
  expect(sys.view()[0].settings.reloadOnRomChange).toBe(true);
});

test("a loaded project's saved gain and core config reach its system's node in the load's document", () => {
  // Nothing else carries a saved gain to a core, which is built at 0 dB: the document is what makes it play at the
  // gain it was saved with.
  const { be, p, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const c = sys.addSystem("/roms/c.nes")!;
  sys.setGain(a, -12);
  sys.setRoleConfig(a, "sameboy", { model: "mgb" });
  sys.setRoleConfig(c, "mesen", { expansionVolume: 60, s5bNoise: "chip" });
  const bytes = p.exportBytes()!;
  p.newProject();
  const calls = be.paramCalls.length;
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const [la, lc] = sys.systems().map((e) => e.id);
  expect(be.graphNode(`sys-${la}`)?.params).toEqual(canon({ gain: -12 }));
  expect(be.graphNode(`sys-${la}`)?.settings?.model).toBe("mgb");
  expect(be.graphNode(`sys-${lc}`)?.params).toEqual(canon({ gain: 0, expansionVolume: 60 }));
  expect(be.graphNode(`sys-${lc}`)?.settings?.s5bNoise).toBe("chip");
  expect(be.paramCalls.length).toBe(calls); // the document, not a live call
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
  expect(sys.graphSystems().map((s) => s.linkGroup)).toEqual([1, 1]);
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
  const putResource = be.putResource.bind(be);
  let roms = 0;
  be.putResource = (spec) => (spec.type === "rom" && ++roms === 2 ? { error: "could not read it" } : putResource(spec));
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/")).toEqual({ kind: "loaded", systems: 2 });
  const [first, third] = sys.systems().map((e) => e.id);
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: [first, third], tap: {} },
  ]);
});

test("one throwing systems observer does not skip the ones after it, nor fail the edit", () => {
  const { p, sys } = project();
  const warned: string[] = [];
  const warn = console.warn;
  console.warn = (...a: unknown[]) => void warned.push(a.join(" "));
  let heard = 0;
  try {
    p.addOnSystemsChange(() => {
      throw new Error("an observer died");
    });
    p.addOnSystemsChange(() => void heard++);
    expect(sys.addSystem("/roms/a.gb") !== null).toBe(true); // the edit itself completes
  } finally {
    console.warn = warn;
  }
  expect(heard).toBe(1);
  expect(warned.length).toBe(1);
  expect(warned[0].includes("an observer died")).toBe(true);
});

test("a load that throws partway places what it adopted, and the edits after it plan", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const putResource = be.putResource.bind(be);
  let roms = 0;
  be.putResource = (spec) => {
    if (spec.type === "rom" && ++roms === 2) throw new Error("the putResource RPC died"); // as an RPC error reply throws
    return putResource(spec);
  };
  const n = be.graphRequests.length;
  let notified = 0;
  p.addOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the putResource RPC died");
  be.putResource = putResource;
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

test("a load that throws partway drops a system the placement refuses, as the success path does", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const putResource = be.putResource.bind(be);
  let roms = 0;
  be.putResource = (spec) => {
    if (spec.type === "rom" && ++roms === 2)
      throw new Error("the putResource RPC died"); // as an RPC error reply throws
    return putResource(spec);
  };
  // The system the throw left adopted is refused when the catch places it, as a core that would not build is: the
  // catch path drops the system the refusal names and publishes the rest, instead of leaving the store listing a
  // system the engine does not run.
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    if (request.slots.length === 0) return setGraph(request);
    be.graphRequests.push(request);
    const result: SetGraphResult = {
      ok: false,
      errors: [
        {
          nodeId: `sys-${request.slots[0]}`,
          port: "",
          code: "activate-failed",
          message: "the ROM is not a Game Boy ROM",
        },
      ],
    };
    return result;
  };
  expect(() => p.loadBytes(bytes, "/")).toThrow("the putResource RPC died");
  be.putResource = putResource;
  be.setGraph = setGraph;
  expect(sys.systems()).toEqual([]); // dropped, with the note the refusal gives
  expect(routed(be)).toEqual([]); // and the document the engine runs places nothing
});

test("a load's retry drops only a system the refusal names, and native's slots refusal names none", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  const bytes = p.exportBytes()!;
  const setGraph = be.setGraph.bind(be);
  // Native's slots check answers `nodeId: ""` for a document its nodes and slots disagree on (Engine.cpp): the
  // document's fault, not one system's, so the retry has no system to drop and leaves the model as it is.
  be.setGraph = (request) => {
    if (request.slots.length === 0) return setGraph(request);
    be.graphRequests.push(request);
    return {
      ok: false,
      errors: [
        {
          nodeId: "",
          port: "",
          code: "slots",
          message: "the slots and the document's system nodes name different systems",
        },
      ],
    };
  };
  const { result: outcome, warnings } = warned(() => p.loadBytes(bytes, "/"));
  be.setGraph = setGraph;
  expect(outcome.kind).toBe("loaded");
  expect(sys.systems().length).toBe(1); // not dropped in turn: the refusal describes the document, not a system
  // The store still lists the system, so the load says why nothing runs instead of returning silently.
  expect(warnings.some((w) => w.includes("naming no system"))).toBe(true);
});

test("a load's retry drops a system whose protocol node the refusal names, and places the rest", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const lsdj = sys.addSystem("/roms/lsdj.gb")!;
  sys.setRoleConfig(lsdj, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
  const bytes = p.exportBytes()!;
  const setGraph = be.setGraph.bind(be);
  let bad = 0;
  // Native reports a protocol node's own failure under `sys-<id>/<kind>` (Planner.cpp), and that node belongs to
  // the system, so the load drops it as it drops one whose `sys-<id>` node the refusal names.
  be.setGraph = (request) => {
    if (!bad && request.slots.length > 0) {
      bad = request.slots[request.slots.length - 1];
      be.graphRequests.push(request);
      return {
        ok: false,
        errors: [
          {
            nodeId: `sys-${bad}/lsdj-sync`,
            port: "",
            code: "activate-failed",
            message: "the script host refused its settings",
          },
        ],
      };
    }
    return setGraph(request);
  };
  const { warnings, result } = warned(() => p.loadBytes(bytes, "/"));
  be.setGraph = setGraph;
  expect(result).toEqual({ kind: "loaded", systems: 2 });
  expect(sys.systems().map((e) => e.id).includes(bad)).toBe(false); // the refused system is gone
  const placed = be.lastDocument()!.nodes.filter((n) => n.id.startsWith("sys-")).map((n) => n.id);
  expect(placed.length).toBe(2);
  expect(placed.includes(`sys-${bad}`)).toBe(false);
  expect(warnings.some((w) => w.includes("the engine refused it, so the project loads without it"))).toBe(true);
});

test("a load whose final publish throws still tells the observers once", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const putResource = be.putResource.bind(be);
  const setGraph = be.setGraph.bind(be);
  let roms = 0;
  be.putResource = (spec) => {
    if (spec.type === "rom") roms++;
    return putResource(spec);
  };
  // The load's clear publishes before any boot, and its final publish after both: that one dies, as an RPC would.
  be.setGraph = (...args) => {
    if (roms === 2) throw new Error("the setGraph RPC died");
    return setGraph(...args);
  };
  let notified = 0;
  p.addOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the setGraph RPC died");
  be.putResource = putResource;
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
  p.addOnSystemsChange(() => notified++);
  expect(() => p.loadBytes(bytes, "/")).toThrow("the setGraph RPC died");
  be.setGraph = setGraph;
  expect(sys.systems().length).toBe(0);
  expect(notified).toBe(1);
  // The failed load placed what it listed, nothing: the clear's document, sent again.
  expect(routed(be)).toEqual([]);
});

test("an adopt whose settings do not parse throws before it puts a resource", () => {
  const { be, sys } = project();
  expect(() => sys.adopt({ romPath: "/roms/a.gb", settings: "loud" as never })).toThrow();
  expect(be.putResourceCalls.length).toBe(0);
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

test("a system in an LSDj MIDI-out mode captures its serial out as its document says: MI.OUT drives, master sync listens", () => {
  const { be, sys } = project();
  let id = sys.addSystem("/roms/lsdj.gb")!;
  const placed = () => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
  expect(placed()).toBe("off"); // midiSync by default
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(placed()).toBe("drive"); // in the document that hands its decoder the mode
  id = sys.reset(id)!; // a rebuild, under a new id: the document that places it carries the capture its build does not
  expect(placed()).toBe("drive");
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
  expect(placed()).toBe("listen");
  id = sys.reset(id)!;
  expect(placed()).toBe("listen");
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
  expect(placed()).toBe("off");
  // No resource a boot puts carries a capture: it lives in the node's settings alone.
  expect(be.putResourceCalls.every((c) => !("serialOutCapture" in (c.spec as Record<string, unknown>)))).toBe(true);
});

test("a cart the controller drives in MI.MAP captures nothing, and gets its stored mode's capture back with the controller off", () => {
  const { be, p, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  const placed = () => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
  expect(placed()).toBe("drive");
  p.setSection(CONTROLLER_SECTION, { enabled: true, app: "lsdj-midimap", target: "system", systemId: id });
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe("midiMap");
  expect(placed()).toBe("off"); // in the one document that forces the node's mode
  p.setSection(CONTROLLER_SECTION, { enabled: false });
  expect(placed()).toBe("drive");
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
  expect(be.protocolNodes(a)).toEqual(roleNodes(a, "lsdj-sync"));
  expect(be.protocolNodes(c)).toEqual([`sys-${c}/nes-n8-midi`]);
  // The Game Boy's serial out goes forward into its lsdj-sync role's decoder, in the block the core captured it.
  expect(be.lastDocument()!.wires.filter((w) => w.feedback)).toEqual([]);
  expect(wired(be, `sys-${a}.serialOut`, `sys-${a}/lsdj-sync/out.serialOut`)).toBe(true);
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
  // The capture rides the same document, on the Game Boy's node, so the two change in one plan.
  expect(be.graphNode(`sys-${id}`)?.settings?.serialOutCapture).toBe("drive");
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(since(be, n).length).toBe(1);
});

test("a controller session is a node of the document, with the sync mode its cart needs", () => {
  const { be, p, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  const n = be.graphRequests.length;
  p.setSection(CONTROLLER_SECTION, { enabled: true, app: "lsdj-midimap", target: "system", systemId: id });
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
  p.setSection(CONTROLLER_SECTION, { enabled: false });
  expect(be.graphNode("controller")).toBe(undefined);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe(LsdjSyncMode.MidiSync);
});

test("a load hands the document its project's MIDI routing and controller in the one publish that places its systems", () => {
  // The settings are assigned after the adopts and before that publish. The plugin's setState is a load, so a
  // publish that read the settings before them would route by the previous project's until the next edit.
  const { be, p, sys } = project();
  sys.addSystem("/roms/lsdj.gb");
  p.setMidiRouting(MidiRouting.MidiChannelToInstance);
  p.setSection(CONTROLLER_SECTION, { enabled: true, app: "lsdj-midimap", target: "system", systemId: 0 });
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
  // The node types are code in the script bundle, so a role registered at runtime has no node to be placed as
  // (graph/protocolNodes.ts).
  const registry = buildAppRegistry();
  registry.registerRole({ kind: "probe", category: "feature", schema: z.object({ level: clampedInt(0, 10, 1) }) });
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
  expect(be.protocolNodes(id)).toEqual(roleNodes(id, "lsdj-sync"));
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings).toEqual({ mode: LsdjSyncMode.MidiSync });
  // A store on its own routes no MIDI and runs no controller.
  expect(be.graphNode("routing")).toBe(undefined);
  const n = be.graphRequests.length;
  expect(sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut })).toBe(true);
  expect(since(be, n)).toEqual([{ slots: [id] }]);
  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings).toEqual({ mode: LsdjSyncMode.MidiOut });
});

// The script node types the engine registers, read from the bundle, each with the settings check its script host runs.
const SCRIPT_NODES = new Map(
  ((globalThis as Record<string, unknown>)[SCRIPT_NODES_GLOBAL] as DefinedNode<any>[]).map((d) => [d.id, d]),
);
// The C++ node types buildGraph places. A type in neither this nor the bundle is one the engine registers nowhere.
const NATIVE_TYPES = new Set([
  "rp.host.midi-in", "rp.host.midi-out", "rp.host.audio-out", "rp.core.split", "rp.core.add",
  "rp.launchpad.surface-in", "rp.launchpad.surface-out", "rp.n8.nes",
  "rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms",
]);

// What the engine's planner would refuse `doc` for, of the refusals a document TS builds can earn it: a node id used
// twice (node-id), a type nothing registers (unknown-type), a script node's settings its type refuses
// (settings-invalid), and a second producer wired into one input (input-taken). Any of them refuses the whole
// document. Empty when none applies. It fails closed: a type it does not know is a refusal, not a pass.
function refusals(doc: GraphDocument): string[] {
  const out: string[] = [];
  const ids = new Set<string>();
  for (const n of doc.nodes) {
    if (ids.has(n.id)) out.push(`node-id ${n.id}`);
    ids.add(n.id);
    const def = SCRIPT_NODES.get(n.type);
    if (!def) {
      if (!NATIVE_TYPES.has(n.type)) out.push(`unknown-type ${n.id}`);
      continue;
    }
    try {
      def.parseSettings(n.settings);
    } catch (e) {
      out.push(`settings-invalid ${n.id}: ${String(e)}`);
    }
  }
  const fed = new Set<string>();
  for (const w of doc.wires) {
    if (fed.has(w.to)) out.push(`input-taken ${w.to}`);
    fed.add(w.to);
  }
  return out;
}

test("the refusals check refuses what the planner would: a type nothing registers, an id twice, an input fed twice", () => {
  // It fails closed, so a node type buildGraph starts placing is named here before a document of it is called sound.
  const doc = (nodes: GraphDocument["nodes"], wires: GraphDocument["wires"] = []): GraphDocument => ({ nodes, wires });
  expect(refusals(doc([{ id: "x", type: "rp.no.such-type" }]))).toEqual(["unknown-type x"]);
  const twice = doc([{ id: "sys-1", type: "rp.sameboy.gb" }, { id: "sys-1", type: "rp.sameboy.gb" }]);
  expect(refusals(twice)).toEqual(["node-id sys-1"]);
  const fed = [{ from: "a/mgb.serialIn", to: "sys-1.serialIn" }, { from: "b/mgb.serialIn", to: "sys-1.serialIn" }];
  expect(refusals(doc([{ id: "sys-1", type: "rp.sameboy.gb" }], fed))).toEqual(["input-taken sys-1.serialIn"]);
  const bad = refusals(doc([{ id: "s", type: "rp.lsdj.sync", settings: { mode: "midiSyncTurbo" } }]));
  expect(bad.length === 1 && bad[0].startsWith("settings-invalid s:")).toBe(true);
});

// Stored configs a load can hand a protocol role: every field missing, unknown enum values, integers out of range or
// not whole, wrong types, nulls, nested values, a key no schema reads, and not an object at all.
const ADVERSARIAL: unknown[] = [
  {},
  { mode: "midiSyncTurbo", machine: "md" },
  { tempoDivisor: 99 },
  { tempoDivisor: -3 },
  { tempoDivisor: 2.5 },
  { tempoDivisor: Number.NaN },
  { mode: 7, tempoDivisor: "4", autoStart: "yes", machine: 1 },
  { mode: null, tempoDivisor: null, autoStart: null, machine: null },
  { mode: ["midiOut"], tempoDivisor: { n: 2 }, machine: { gg: true } },
  { mode: "midiOut", machine: "gg", futureKnob: 3 },
  [],
];

test("whatever a loaded project stores for a protocol role, the store hands its node settings the node accepts", () => {
  // What a load's repair rests on: each role's schema, and the fallback when it refuses, yield settings its node's
  // strict schema takes, for every kind with a protocol node. A kind added to the table without a cart here fails.
  const { be, sys } = project();
  be.seed("/roms/smsggdj.sms", smsggdjRom());
  const carts: Record<string, { romPath?: string; embeddedRom?: string }> = {
    mgb: { embeddedRom: "mgb" },
    "nes-n8-midi": { romPath: "/roms/c.nes" },
    "risa-sync": { romPath: "/roms/c.nes" },
    "sms-sync": { romPath: "/roms/smsggdj.sms" },
    "lsdj-sync": { romPath: "/roms/lsdj.gb" },
  };
  expect(Object.keys(carts).sort()).toEqual(Object.keys(ROLE_NODES).sort());
  for (const [kind, cart] of Object.entries(carts)) {
    for (const config of ADVERSARIAL) {
      const what = `${kind} ${JSON.stringify(config)}`;
      const roles = [{ kind, config: config as Record<string, unknown> }];
      const id = warned(() => sys.adopt({ ...cart, roles })).result;
      expect(id !== null, what).toBe(true);
      const node = be.graphNode(`sys-${id}/${kind}`);
      expect(node?.type, what).toBe(ROLE_NODES[kind]);
      expect(refusals(be.lastDocument()!), what).toEqual([]);
      sys.removeSystem(id!);
    }
  }
});

// A thin project file at `path`, stamped `version`, holding one system: the LSDj cart with `roles`.
function seedLsdjProject(be: MockBackend, path: string, roles: RoleInstance[], version = "4"): void {
  const systems = [{ platform: "gb", core: "sameboy", romPath: "/roms/lsdj.gb", roles }];
  be.seed(path, new TextEncoder().encode(JSON.stringify({ schemaVersion: version, settings: {}, systems })));
}

test("a loaded lsdj-sync config this build refuses reaches its node repaired, and the project plans", () => {
  // A store parses a role's config on an edit and fills a fresh system's defaults, and a load must too: a newer
  // build's added mode (additive, so no version bump), a v2 file's out-of-range mode index (the v3 step leaves it) or a
  // hand edit would reach the node as written, whose settings are strict, and the planner refuses the whole document
  // for one node's settings: the project would play nothing.
  const { be, p, sys } = project();
  seedLsdjProject(be, "/proj/newer.rplg", [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: "midiSyncTurbo", tempoDivisor: 12, autoStart: "yes" } },
  ]);
  seedLsdjProject(be, "/proj/v2.rplg", [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: 9, tempoDivisor: 4 } },
  ], "2");
  const answered = answers(be);
  const cases: [string, Record<string, unknown>][] = [
    ["/proj/newer.rplg", { mode: LsdjSyncMode.MidiSync, tempoDivisor: 8, autoStart: false }],
    ["/proj/v2.rplg", { mode: LsdjSyncMode.MidiSync, tempoDivisor: 4, autoStart: false }],
  ];
  for (const [path, repaired] of cases) {
    const { result, warnings } = warned(() => p.load(path));
    expect(result.kind).toBe("loaded");
    const [id] = sys.systems().map((e) => e.id);
    const settings = be.graphNode(`sys-${id}/lsdj-sync`)?.settings;
    expect(settings).toEqual(canon(repaired));
    expect(lsdjSyncNode.parseSettings(settings)).toEqual(repaired);
    expect(refusals(be.lastDocument()!)).toEqual([]);
    // The system holds the config its node runs, so its menu shows it and a save writes it.
    expect(sys.view()[0].roles.find((r) => r.kind === "lsdj-sync")?.config).toEqual(repaired);
    expect(warnings.length).toBe(1);
  }
  expect(answered.every((r) => r.ok)).toBe(true);
});

test("of two roles whose nodes write serialIn, a loaded system places the one its ROM calls for, either way round", () => {
  // A project stored with mgb, its ROM path holding LSDj today, gains lsdj-sync as a missing feature role (and the
  // reverse for an mGB cart), and both nodes write the system's serialIn: a second producer into one input, for which
  // the planner refuses the document. The role today's ROM calls for is placed; the stale one, kept on the system so a
  // save keeps it, is left out.
  const { be, sys } = project();
  be.seed("/roms/mgb.gb", lsdjRom("MGB"));
  const sameboy: RoleInstance = { kind: "sameboy", config: {} };
  const cases: [string, RoleInstance, string, string][] = [
    ["/roms/lsdj.gb", { kind: "mgb", config: {} }, "lsdj-sync", "mgb"],
    ["/roms/mgb.gb", { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync } }, "mgb", "lsdj-sync"],
  ];
  for (const [romPath, stale, placed, left] of cases) {
    const { result: id, warnings } = warned(() => sys.adopt({ romPath, core: "sameboy", roles: [sameboy, stale] })!);
    // The role the ROM calls for goes ahead of the stale one, which is the order a save then writes.
    const kinds = sys.view().find((v) => v.id === id)!.roles.map((r) => r.kind);
    expect(kinds.slice(0, 3)).toEqual(["sameboy", placed, left]);
    expect(be.protocolNodes(id)).toEqual(roleNodes(id, placed));
    const serialIn = be.lastDocument()!.wires.filter((w) => w.to === `sys-${id}.serialIn`);
    expect(serialIn.map((w) => w.from)).toEqual([`sys-${id}/${placed}.serialIn`]);
    expect(refusals(be.lastDocument()!)).toEqual([]);
    expect(warnings.length).toBe(1);
    // An edit to the role left out leaves it out, and says nothing more.
    const again = warned(() => sys.setRoleConfig(id, left, {}));
    expect(again.result).toBe(true);
    expect(be.protocolNodes(id)).toEqual(roleNodes(id, placed));
    expect(again.warnings).toEqual([]);
    sys.removeSystem(id);
  }
});

test("of two colliding roles neither of which the ROM calls for, the first in role order is placed", () => {
  const { be, sys } = project();
  const roles: RoleInstance[] = [
    { kind: "sameboy", config: {} },
    { kind: "mgb", config: {} },
    { kind: "lsdj-sync", config: {} },
  ];
  const { result: id, warnings } = warned(() => sys.adopt({ romPath: "/roms/a.gb", core: "sameboy", roles })!);
  expect(be.protocolNodes(id)).toEqual([`sys-${id}/mgb`]);
  expect(sys.view()[0].roles.map((r) => r.kind)).toEqual(["sameboy", "mgb", "lsdj-sync"]); // as stored
  expect(warnings.length).toBe(1);
});

test("a collision's outcome survives a save and a load: the ROM's role first and placed, the stale one kept after it", () => {
  const { be, p, sys } = project();
  seedLsdjProject(be, "/proj/collide.rplg", [
    { kind: "sameboy", config: {} },
    { kind: "mgb", config: {} },
    { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiMap } },
  ]);
  const settle = (path: string) => {
    const { result, warnings } = warned(() => p.load(path));
    expect(result.kind).toBe("loaded");
    const [entry] = sys.systems();
    const kinds = sys.view().find((v) => v.id === entry.id)!.roles.map((r) => r.kind);
    return { id: entry.id, kinds, placed: be.protocolNodes(entry.id), warnings };
  };
  const first = settle("/proj/collide.rplg");
  expect(first.kinds.slice(0, 3)).toEqual(["sameboy", "lsdj-sync", "mgb"]); // moved ahead of the stale role
  expect(first.placed).toEqual(roleNodes(first.id, "lsdj-sync"));
  expect(first.warnings.length).toBe(1);
  expect(be.graphNode(`sys-${first.id}/lsdj-sync`)?.settings).toEqual(canon({ mode: LsdjSyncMode.MidiMap, tempoDivisor: 1, autoStart: false }));
  expect(p.save("/proj/saved.rplg")).toBe(true);
  const again = settle("/proj/saved.rplg");
  expect(again.kinds).toEqual(first.kinds);
  expect(again.placed).toEqual(roleNodes(again.id, "lsdj-sync"));
  expect(again.warnings.length).toBe(1); // the stale role is still kept, so it is still noted
  expect(be.graphNode(`sys-${again.id}/lsdj-sync`)?.settings).toEqual(canon({ mode: LsdjSyncMode.MidiMap, tempoDivisor: 1, autoStart: false }));
});

test("a suggested role that collides with nothing keeps its place in the stored order", () => {
  const { be, p, sys } = project();
  // A stored kind with no protocol node is kept and collides with nothing, and it is not one the ROM suggests: if
  // lsdj-sync were hoisted past it, or past anything, the order would move.
  seedLsdjProject(be, "/proj/order.rplg", [
    { kind: "sameboy", config: {} },
    { kind: "retired-x", config: {} },
    { kind: "lsdj-sync", config: {} },
  ]);
  const { result, warnings } = warned(() => p.load("/proj/order.rplg"));
  expect(result.kind).toBe("loaded");
  const [entry] = sys.systems();
  expect(sys.view().find((v) => v.id === entry.id)!.roles.map((r) => r.kind).slice(0, 3)).toEqual(["sameboy", "retired-x", "lsdj-sync"]);
  expect(be.protocolNodes(entry.id)).toEqual(roleNodes(entry.id, "lsdj-sync"));
  expect(warnings).toEqual([]);
});

test("a kind stored twice: the first is placed and is the one an edit reaches, and the second is noted once", () => {
  const { be, sys } = project();
  const roles: RoleInstance[] = [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync, tempoDivisor: 2 } },
    { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiMap, tempoDivisor: 4 } },
  ];
  const { result: id, warnings } = warned(() => sys.adopt({ romPath: "/roms/lsdj.gb", core: "sameboy", roles })!);
  expect(warnings.length).toBe(1);
  expect(warnings[0].includes("its second lsdj-sync role is not placed")).toBe(true);
  expect(be.protocolNodes(id)).toEqual(roleNodes(id, "lsdj-sync"));
  const settings = () => be.graphNode(`sys-${id}/lsdj-sync`)?.settings;
  expect(settings()).toEqual(canon({ mode: LsdjSyncMode.MidiSync, tempoDivisor: 2, autoStart: false }));
  // An edit reaches the first role, the placed one; the second keeps what it was stored with.
  const edit = warned(() => sys.setRoleConfig(id, "lsdj-sync", { tempoDivisor: 8 }));
  expect(edit.result).toBe(true);
  expect(edit.warnings).toEqual([]);
  expect(settings()).toEqual(canon({ mode: LsdjSyncMode.MidiSync, tempoDivisor: 8, autoStart: false }));
  const stored = sys.view().find((v) => v.id === id)!.roles.filter((r) => r.kind === "lsdj-sync");
  expect(stored.map((r) => (r.config as { tempoDivisor: number }).tempoDivisor)).toEqual([8, 4]);
});

test("an adopt whose resources would not put logs nothing about the system it never added", () => {
  const { be, sys } = project();
  be.putResource = () => ({ error: "refused for the test" });
  const roles: RoleInstance[] = [
    { kind: "sameboy", config: {} },
    { kind: "mgb", config: {} },
    { kind: "lsdj-sync", config: { mode: "midiSyncTurbo" } },
  ];
  const { result, warnings } = warned(() => sys.adopt({ romPath: "/roms/lsdj.gb", core: "sameboy", roles }));
  expect(result).toBe(null);
  expect(sys.systems().length).toBe(0);
  // The put's own refusal is said once; the notes it collected about the roles go with the system it never added.
  expect(warnings).toEqual(["[systems] /roms/lsdj.gb: refused for the test"]);
});

test("a stored core config value the system node would refuse is noted, so a dropped knob leaves a trace", () => {
  // A core config stays as written (the build spec carries it), so buildGraph leaves out a value its node would
  // refuse: the core, the document and the stored config then disagree, and only the load can say so. Once the core
  // has built, as every other repair a load makes is logged.
  const { be, sys } = project();
  const config = { model: "cgbF", highpass: 2, fastBoot: "yes", dmgPalette: "gbl", lightTemperature: 1.5 };
  const stored = (romPath: string, over: Record<string, unknown>) =>
    sys.adopt({ romPath, core: "sameboy", roles: [{ kind: "sameboy", config: over }] })!;
  const { result: id, warnings } = warned(() => stored("/roms/a.gb", config));
  expect(id !== null).toBe(true);
  // The value the node takes is the one the document carries; the rest are the load's to report.
  expect(knobs(be, id)).toEqual(canon({ dmgPalette: "gbl", serialOutCapture: "off" }));
  expect(warnings).toEqual([
    '[systems] /roms/a.gb: its stored sameboy config holds a value this build does not read as written: ' +
      'dropped model "cgbF", dropped highpass 2, dropped fastBoot "yes", dropped lightTemperature 1.5',
  ]);
  // A value absent is not a value refused, and one the node takes says nothing.
  expect(warned(() => stored("/roms/b.gb", { model: "dmgB" })).warnings).toEqual([]);
  // A core config's param is a knob too: a value that is not a finite number cannot ride a document's param, so it is
  // dropped like a refused setting field and noted with it.
  const nes = (over: Record<string, unknown>) =>
    sys.adopt({ romPath: "/roms/c.nes", core: "mesen", roles: [{ kind: "mesen", config: over }] })!;
  const dropped = warned(() => nes({ region: "PAL", expansionVolume: "40" }));
  expect(knobs(be, dropped.result!)).toEqual(canon(NES_CONSTRUCT));
  expect(be.graphNode(`sys-${dropped.result}`)?.params).toEqual({ gain: 0 }); // the param is left out, not sent
  expect(dropped.warnings).toEqual([
    '[systems] /roms/c.nes: its stored mesen config holds a value this build does not read as written: ' +
      'dropped region "PAL", dropped expansionVolume "40"',
  ]);
  expect(warned(() => nes({ expansionVolume: null })).warnings).toEqual([
    '[systems] /roms/c.nes: its stored mesen config holds a value this build does not read as written: ' +
      'dropped expansionVolume null',
  ]);
  // A construct-time field is a node setting too, so an unreadable one is dropped the same way: the document names the
  // node's own default in its place, and the load says so. An unknown label's schema default equals its native
  // default, but a value the schema would clamp or round does not: the node's own default applies, not the repaired
  // value. A finite fraction rides the param, as any number does, and the planner rounds it.
  const construct = warned(() => nes({ region: "pal", fifo: "futureBuild", expansionVolume: 25.7, sdRoot: "/sd" }));
  expect(construct.warnings).toEqual([
    '[systems] /roms/c.nes: its stored mesen config holds a value this build does not read as written: ' +
      'dropped fifo "futureBuild"',
  ]);
  const settings = be.bootOf(construct.result!)!.settings;
  expect(settings.region).toBe("pal");
  expect(settings.fifo).toBe("instant"); // the refused label's place is the node's default, written out
  expect(settings.sdRoot).toBe("/sd");
  expect(be.graphNode(`sys-${construct.result}`)?.params).toEqual(canon({ gain: 0, expansionVolume: 25.7 }));
  // The store still keeps what the file said, for a build that knows the label.
  const kept = sys.view().find((v) => v.id === construct.result)!.roles.find((r) => r.kind === "mesen")!.config;
  expect(kept.fifo).toBe("futureBuild");
  expect(kept.expansionVolume).toBe(25.7);
  // A construct-time integer field is an integer natively (reflect-cpp reads it with static_cast<int>), so a fraction
  // is a value the node would not read as written: left out and noted like any other, never silently truncated.
  const fractional = warned(() =>
    nes({ fifoDepth: 4096.5, fifoBytesPerSecond: 1_000.25, n8LinkRttUs: 2200.5 }),
  );
  expect(fractional.warnings).toEqual([
    '[systems] /roms/c.nes: its stored mesen config holds a value this build does not read as written: ' +
      'dropped fifoDepth 4096.5, dropped fifoBytesPerSecond 1000.25, dropped n8LinkRttUs 2200.5',
  ]);
  const fractionalSettings = be.bootOf(fractional.result!)!.settings;
  expect(fractionalSettings.fifoDepth).toBe(0);
  expect(fractionalSettings.fifoBytesPerSecond).toBe(0);
  expect(fractionalSettings.n8LinkRttUs).toBe(2200);
});

test("an adopt never reports a key its document supplies itself (a Game Gear's gameGear)", () => {
  const { be, sys } = project();
  const gg = smsggdjRom();
  gg[0x7ff0 + 0xf] = 0x50; // the region nibble says Game Gear
  be.seed("/roms/a.gg", gg);
  const { result, warnings } = warned(() =>
    sys.adopt({
      romPath: "/roms/a.gg",
      core: "mesen",
      roles: [{ kind: "mesen", config: { gameGear: true } }],
    }),
  );
  expect(result !== null).toBe(true);
  // A Game Gear's `gameGear` is not a stored knob the schema dropped: buildGraph writes it from the platform.
  expect(warnings.filter((w) => w.includes("gameGear"))).toEqual([]);
  expect(be.bootOf(result!)!.settings.gameGear).toBe(true);
});

test("a hand-edited sdRoot that is not a string loads at the node's empty root, not a failed load", () => {
  // A hand edit for "no SD root" is null. The document cannot carry it (the node reads a string), so the node's own
  // default - the empty root, a per-process scratch directory - is written in its place: the load adopts the system
  // rather than failing on a value the whole plan would be refused for.
  const { be, sys } = project();
  const { result: id, warnings } = warned(() =>
    sys.adopt({ romPath: "/roms/c.nes", core: "mesen", roles: [{ kind: "mesen", config: { sdRoot: null } }] }),
  );
  expect(id !== null).toBe(true);
  expect((be.bootOf(id!)?.settings ?? {}).sdRoot).toBe("");
  expect(warnings).toEqual([
    '[systems] /roms/c.nes: its stored mesen config holds a value this build does not read as written: ' +
      'dropped sdRoot null',
  ]);
});

test("a left-out lsdj-sync arms no serial-out capture: not where it is placed, nor on an edit", () => {
  // Capture is for the lsdj-sync role's decoder, rp.lsdj.serial-out, which decodes what the core sends (MI.OUT
  // drives, master sync listens). On an mGB cart whose project stored lsdj-sync, mgb wins serialIn and the role's
  // nodes are not placed, so nothing would read the capture, and a driving core would clock the serial port under
  // mgb's bytes.
  const { be, sys } = project();
  be.seed("/roms/mgb.gb", lsdjRom("MGB"));
  const roles: RoleInstance[] = [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiOut } },
  ];
  const placed = (id: number) => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
  let id = warned(() => sys.adopt({ romPath: "/roms/mgb.gb", core: "sameboy", roles })!).result;
  expect(be.protocolNodes(id)).toEqual([`sys-${id}/mgb`]);
  expect(placed(id)).toBe("off"); // a load, stored in MI.OUT
  for (const mode of [LsdjSyncMode.MasterSync, LsdjSyncMode.MidiOut]) {
    expect(sys.setRoleConfig(id, "lsdj-sync", { mode })).toBe(true);
    expect(placed(id)).toBe("off"); // an edit
    id = sys.reset(id)!;
    expect(placed(id)).toBe("off"); // a rebuild
  }
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
  const puts = be.putResourceCalls.length;
  expect(g.remove(9)).toBe(true);
  expect(g.replace(9, MGB, 5)).toBe(false);
  expect(since(be, n)).toEqual([
    { slots: [4, 3], tap: {} },
    { slots: [4, 3], tap: {} },
  ]);
  expect(be.putResourceCalls.length).toBe(puts); // nothing was put for the replace of a system that is not placed
});

test("DirectGraph's core config: a link group rewires, a setting rides its node, an unknown field is refused", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  g.add(MGB, 2);
  // None until set: the node carries no setting field, so its core keeps the settings it was built with (its gain
  // param still starts at its default, 0 dB, at its first block).
  expect(knobs(be, 1)).toEqual({ serialOutCapture: "off" });
  const n = be.graphRequests.length;
  expect(g.setCoreConfig(1, { linkGroupId: 1 })).toBe(true);
  expect(g.setCoreConfig(2, { linkGroupId: 1 })).toBe(true);
  expect(since(be, n)).toEqual([{ slots: [1, 2] }]); // a group of one is no cable: the first sends nothing
  expect(linked(be, "sys-2.link")).toBe(true);
  // Merged over what it had: the group stays, the model rides the node's settings as its label.
  expect(g.setCoreConfig(2, { model: "dmgB", highpass: "off" })).toBe(true);
  expect(linked(be, "sys-2.link")).toBe(true);
  const sys2 = canon({ model: "dmgB", highpass: "off", serialOutCapture: "off" });
  expect(knobs(be, 2)).toEqual(sys2);
  // An ordinal, a construct-time field, another core's field, a key every object inherits: each refused, changing
  // nothing.
  const sent = be.graphRequests.length;
  for (const partial of [{ model: 1 }, { channelExportMode: "pins" }, { region: "pal" }, { toString: "loud" },
    { linkGroupId: -1 }, { model: "mgb", fifo: 1 }]) // the last: one bad field refuses the whole edit
    expect(g.setCoreConfig(2, partial), JSON.stringify(partial)).toBe(false);
  expect(g.setCoreConfig(9, { model: "mgb" })).toBe(false); // not placed
  expect(be.graphRequests.length).toBe(sent);
  expect(be.graphNode("sys-2")?.settings?.model).toBe("dmgB");
  // The next document, another system's edit, carries the config last taken: a refused edit left nothing behind.
  expect(g.setCoreConfig(1, { model: "mgb" })).toBe(true);
  expect(knobs(be, 2)).toEqual(sys2);
});

test("DirectGraph's params: set live with no publish, and carried by the next document", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  const n = be.graphRequests.length;
  expect(g.setParam(1, "gain", -12)).toBe(true);
  expect(be.paramCalls).toEqual([{ nodeId: "sys-1", paramId: "gain", value: -12 }]);
  expect(be.graphRequests.length).toBe(n);
  expect(g.setParam(1, "level", 1)).toBe(false); // no system node's param
  expect(g.setParam(9, "gain", 0)).toBe(false); // not placed
  expect(be.paramCalls.length).toBe(1);
  g.setCoreConfig(1, { highpass: "off" });
  expect(be.graphNode("sys-1")?.params).toEqual({ gain: -12 });
});

test("DirectGraph sends the document again after a live param moved its node away from it", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/c.nes", nesRom());
  const g = new DirectGraph(be);
  const nes = { romPath: "/roms/c.nes", platform: "nes" as const, core: "mesen" as const, embeddedRom: "", savPath: null, statePath: null };
  expect(g.add(nes, 1)).toBe(true);
  expect(g.setCoreConfig(1, { expansionVolume: 200 })).toBe(true);
  expect(g.setParam(1, "expansionVolume", 100)).toBe(true); // live: the node is at 100, the last document at 200
  const n = be.graphRequests.length;
  // The model names 200 again, the value the last SENT document carried, but the node does not have it: the document
  // is sent (the engine then compares against the value the live edit recorded) rather than skipped as unchanged.
  expect(g.setCoreConfig(1, { expansionVolume: 200 })).toBe(true);
  expect(be.graphRequests.length).toBe(n + 1);
  expect(be.graphNode("sys-1")?.params).toEqual({ expansionVolume: 200 });
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

test("DirectGraph sets each system's pipeline and the MIDI routing, each a publish without a re-arm", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  g.add(MGB, 2);
  // No roles: no protocol node, no routing and no controller (so no surface either).
  expect(be.lastDocument()!.nodes.map((n) => n.id).slice(0, 4)).toEqual(["midi-in", "sys-1", "sys-2", "midi-out"]);
  const n = be.graphRequests.length;
  const mgb: RoleInstance = { kind: "mgb", config: {} };
  expect(g.setPipeline(2, [{ kind: "sameboy", config: {} }, mgb])).toBe(true); // a role with no node is left out
  expect(g.setMidiRouting("sendToAll")).toBe(true);
  expect(g.setPipeline(9, [mgb])).toBe(false); // not placed here
  expect(since(be, n)).toEqual([{ slots: [1, 2] }, { slots: [1, 2] }]);
  expect(be.graphNode("routing")?.settings).toEqual({ mode: "sendToAll", systems: [1, 2] });
  expect([be.protocolNodes(1), be.protocolNodes(2)]).toEqual([[], ["sys-2/mgb"]]);
  expect(wired(be, "routing.sys-2", "sys-2/mgb.midi")).toBe(true);
  // A replace is a new system, with no roles; the other keeps its own.
  g.replace(2, MGB, 3);
  g.setPipeline(1, [mgb]);
  expect([be.protocolNodes(1), be.protocolNodes(3)]).toEqual([["sys-1/mgb"], []]);
  // Of two roles whose nodes write one input the first is placed, and the caller hears of the other.
  const left: LeftOutRole[] = [];
  expect(g.setPipeline(1, [mgb, { kind: "lsdj-sync", config: {} }], (r) => left.push(r))).toBe(true);
  expect(be.protocolNodes(1)).toEqual(["sys-1/mgb"]);
  expect(left).toEqual([{ kind: "lsdj-sync", input: "serialIn", by: "mgb" }]);
});

test("DirectGraph takes the project's controller as one of the graph's models, its app config parsed by the placer", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  expect(g.setMidiRouting("sendToAll")).toBe(true);
  const controller = { app: "lsdj-midimap", target: "midiOut", systemId: 0, appConfig: { quantise: "beat" } } as const;
  expect(g.setModel("controller", { ...controller, songRowTicks: [], anchor: null, cartSync: null })).toBe(true);
  expect(be.graphNode("controller")?.settings).toEqual(
    canon({
      app: "lsdj-midimap", target: "midiOut", systemId: 1, appConfig: { quantise: "beat", follow: true, page: 0 },
      songRowTicks: [], anchorRows: [], anchorSeq: 0,
    }),
  );
  // Bound for a real Game Boy, its launches leave the host, under the system it resolved.
  expect(be.graphNode("midi-out")?.settings).toEqual({ systems: [1] });
  expect(wired(be, "controller.midiOut", "midi-out.in1")).toBe(true);
  expect(g.setModel("controller", null)).toBe(true);
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
  expect(g.setMidiRouting("sendToAll")).toBe(false);
  // The restored model is what a shape change builds.
  expect(g.graph.withShape({ kind: "perSystem" }, () => true)).toBe(true);
  expect(be.lastDocument()).toEqual(before);
  expect(be.protocolNodes(1)).toEqual([]);
  expect(be.graphNode("routing")).toBe(undefined);
});
