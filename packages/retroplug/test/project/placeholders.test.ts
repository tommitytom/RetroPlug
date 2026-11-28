// A system this build cannot build because a pack is missing (docs/downloadable-packs-m3-design.md sections 1 to 3):
// the parse keeps it with what it needs, the systems store holds it in a slot of its own, and a save writes its JSON
// and its zip entries back as they came. A malformed system is still dropped. Every id here is this file's own.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { parseConfig } from "../../src/projectConfig";
import { gbRomBattery } from "../systems/fixtures";

const enc = new TextEncoder();
const dec = new TextDecoder();

const FX = { id: "fxpack", name: "FX Pack", version: "2.0.0" };
const FX_TRACKER = { id: "fxtracker", name: "FX Tracker", version: "1.1.0" };

/** A system on a platform no package of this product provides, with a role nothing registers and a member no build
 *  reads: everything a placeholder must write back. */
const PLACEHOLDER = {
  platform: "fx-missing",
  core: "fx-core",
  romPath: "/roms/song.fx",
  savSuffix: 2,
  roles: [{ kind: "fx-core", config: { level: 3 } }, { kind: "fx-marker", config: {} }],
  provenance: { platform: FX, roles: { "fx-marker": FX_TRACKER } },
  futureField: { kept: true, list: [1, 2, 3] },
};

const gb = (romPath: string) => ({ platform: "gb", core: "sameboy", romPath });

function newProject() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  be.seed("/roms/b.gb", gbRomBattery());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  return { be, project };
}

function zipOf(be: MockBackend, systems: unknown[], blobs: Record<string, number[]>): Uint8Array {
  const doc = { schemaVersion: "4", settings: {}, systems };
  return be.zip([
    { name: "project.json", bytes: enc.encode(JSON.stringify(doc)) },
    ...Object.entries(blobs).map(([name, bytes]) => ({ name, bytes: new Uint8Array(bytes) })),
  ])!;
}

test("the parse keeps a system that fails only on a pack-provided name, with its needs, drops a malformed one", () => {
  const cfg = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [
      gb("/roms/a.gb"),
      PLACEHOLDER,
      { platform: "n64-missing", core: "sameboy" },
      { platform: 7, core: "sameboy" },
    ],
  }));
  expect(cfg.systems.map((s) => s.romPath)).toEqual(["/roms/a.gb"]);
  expect(cfg.placeholders?.map((p) => p.index)).toEqual([1, 2]);
  expect(cfg.placeholders?.[0].raw).toEqual(PLACEHOLDER);
  // The platform from its provenance; the core's own config role is the console's, so it is not listed again; a role
  // kind nothing registers, by the pack the file named for it.
  expect(cfg.placeholders?.[0].needs).toEqual([
    { what: "platform", key: "fx-missing", pack: FX, status: "missing" },
    { what: "role", key: "fx-marker", pack: FX_TRACKER, status: "missing" },
  ]);
  // Saved before M3: no provenance, so the need names the platform.
  expect(cfg.placeholders?.[1].needs).toEqual([{ what: "platform", key: "n64-missing", status: "missing" }]);
});

test("an embedded ROM no package declares makes a placeholder at the parse", () => {
  const cfg = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [
      {
        platform: "gb",
        core: "sameboy",
        embeddedRom: "fx-synth",
        provenance: { embed: { id: "fxembeds", name: "FX Embeds" } },
      },
    ],
  }));
  expect(cfg.systems).toEqual([]);
  expect(cfg.placeholders?.[0].needs).toEqual([
    { what: "embed", key: "fx-synth", pack: { id: "fxembeds", name: "FX Embeds" }, status: "missing" },
  ]);
});

test("a placeholder between two live systems keeps its slot, JSON and entries, and the next system its own", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER, gb("/roms/b.gb")], {
    "systems/0/sram": [1, 1, 1],
    "systems/1/sram": [9, 8, 7],
    "systems/1/state": [6, 5],
    "systems/1/song": [4],
    "systems/2/sram": [3, 3, 3],
  });
  const outcome = project.loadBytes(zip, "");
  expect(outcome.kind).toBe("loaded");
  if (outcome.kind !== "loaded") return;
  expect(outcome.systems).toBe(2);
  expect(outcome.missingPacks).toEqual([
    { id: "fxpack", name: "FX Pack", version: "2.0.0", status: "missing", systems: 1, roles: 0 },
    { id: "fxtracker", name: "FX Tracker", version: "1.1.0", status: "missing", systems: 1, roles: 0 },
  ]);
  const slots = project.systems.slotView();
  expect(slots.map((v) => v.placeholder !== undefined)).toEqual([false, true, false]);
  expect(slots[1].platform).toBe("fx-missing");
  expect(slots[1].romPath).toBe("/roms/song.fx");
  expect(project.systems.view().map((v) => v.id)).toEqual([slots[0].id, slots[2].id]);
  expect(be.bootOf(slots[2].id)?.sramBytes).toEqual(new Uint8Array([3, 3, 3]));
  expect(project.isDirty()).toBe(false);

  const entries = be.unzip(project.exportBytes()!)!;
  const byName = (name: string) => entries.find((e) => e.name === name)?.bytes;
  const doc = JSON.parse(dec.decode(byName("project.json")!)) as { systems: unknown[] };
  expect(doc.systems.length).toBe(3);
  expect(doc.systems[1]).toEqual(PLACEHOLDER);
  expect(byName("systems/1/sram")).toEqual(new Uint8Array([9, 8, 7]));
  expect(byName("systems/1/state")).toEqual(new Uint8Array([6, 5]));
  expect(byName("systems/1/song")).toEqual(new Uint8Array([4]));
  // The live systems' entries are the mock pump's, under their own slots' indices.
  expect(byName("systems/0/sram") !== undefined).toBe(true);
  expect(byName("systems/2/sram") !== undefined).toBe(true);
});

test("a file this build wrote saves to identical bytes, its placeholder included", () => {
  const { be, project } = newProject();
  const doc = { schemaVersion: "4", settings: {}, systems: [gb("/roms/a.gb"), PLACEHOLDER] };
  be.seed("/proj/m3.rplg", JSON.stringify(doc));
  expect(project.load("/proj/m3.rplg").kind).toBe("loaded");
  expect(project.save("/proj/one.rplg")).toBe(true);
  expect(project.load("/proj/one.rplg").kind).toBe("loaded");
  expect(project.save("/proj/two.rplg")).toBe(true);
  const one = be.readText("/proj/one.rplg")!;
  expect(be.readText("/proj/two.rplg")).toBe(one);
  // The placeholder's ROM is outside the project's folder, so it stays absolute, exactly as the file had it.
  expect((JSON.parse(one) as { systems: unknown[] }).systems[1]).toEqual(PLACEHOLDER);
});

test("Remove is a placeholder's one edit: the project is dirty, and its slot and entries are gone", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [PLACEHOLDER, gb("/roms/b.gb")], { "systems/0/sram": [9, 8, 7], "systems/1/sram": [3, 3, 3] });
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  const [held, live] = project.systems.slotView();
  expect(held.placeholder !== undefined).toBe(true);
  expect(project.systems.removeSystem(held.id)).toBe(true);
  expect(project.isDirty()).toBe(true);
  expect(project.systems.slotView().map((v) => v.id)).toEqual([live.id]);
  const entries = be.unzip(project.exportBytes()!)!;
  const json = dec.decode(entries.find((e) => e.name === "project.json")!.bytes);
  const doc = JSON.parse(json) as { systems: { romPath: string }[] };
  expect(doc.systems.map((s) => s.romPath)).toEqual(["/roms/b.gb"]);
  // Renumbered with the slots: the live system's entries are systems/0 now, and nothing is left at systems/1.
  expect(entries.some((e) => e.name.startsWith("systems/1/"))).toBe(false);
  expect(entries.some((e) => e.name === "systems/0/sram")).toBe(true);
});

test("a placeholder can take the focus, and the instance it names follows it", () => {
  const { be, project } = newProject();
  expect(project.loadBytes(zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER], {}), "").kind).toBe("loaded");
  const [, held] = project.systems.slotView();
  expect(project.systems.setFocus(held.id)).toBe(true);
  expect(project.systems.slotView()[1].focused).toBe(true);
  expect(project.systems.currentId(held.id)).toBe(held.id);
});

const THREE_BLOBS = {
  "systems/0/sram": [1, 1, 1],
  "systems/1/sram": [9, 8, 7],
  "systems/1/state": [6, 5],
  "systems/2/sram": [3, 3, 3],
};

/** The saved archive's project.json systems' romPaths, and its entry names. */
type Shape = { roms: string[]; names: string[]; bytes: (n: string) => unknown };
function savedShape(be: MockBackend, project: ProjectStore): Shape {
  const entries = be.unzip(project.exportBytes()!)!;
  const doc = JSON.parse(dec.decode(entries.find((e) => e.name === "project.json")!.bytes)) as {
    systems: { romPath: string }[];
  };
  return {
    roms: doc.systems.map((s) => s.romPath),
    names: entries.map((e) => e.name),
    bytes: (n) => entries.find((e) => e.name === n)?.bytes,
  };
}

test("removing a live system closes the slots: the placeholder and the next system renumber", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER, gb("/roms/b.gb")], THREE_BLOBS);
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  const [first] = project.systems.slotView();
  expect(project.systems.removeSystem(first.id)).toBe(true);
  expect(project.systems.slotView().map((v) => v.placeholder !== undefined)).toEqual([true, false]);
  const saved = savedShape(be, project);
  expect(saved.roms).toEqual(["/roms/song.fx", "/roms/b.gb"]);
  expect(saved.bytes("systems/0/sram")).toEqual(new Uint8Array([9, 8, 7]));
  expect(saved.bytes("systems/0/state")).toEqual(new Uint8Array([6, 5]));
  expect(saved.names.some((n) => n.startsWith("systems/2/"))).toBe(false);
  expect(saved.names.includes("systems/1/sram")).toBe(true);
});

test("a discard closes the slot the system held", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER, gb("/roms/b.gb")], THREE_BLOBS);
  // The engine refuses the first document that names a system: the load drops the first system it placed, as it
  // drops one whose node a refusal names.
  const setGraph = be.setGraph.bind(be);
  let bad = 0;
  be.setGraph = (request) => {
    if (!bad && request.slots.length > 0) {
      bad = request.slots[0];
      be.graphRequests.push(request);
      const errors = [{ nodeId: `sys-${bad}`, port: "", code: "activate-failed", message: "refused for the test" }];
      return { ok: false, errors };
    }
    return setGraph(request);
  };
  const warn = console.warn;
  console.warn = () => {};
  try {
    expect(project.loadBytes(zip, "").kind).toBe("loaded");
  } finally {
    console.warn = warn;
    be.setGraph = setGraph;
  }
  expect(project.systems.view().map((v) => v.id).includes(bad)).toBe(false);
  expect(project.systems.slotView().map((v) => v.placeholder !== undefined)).toEqual([true, false]);
  expect(project.systems.placeholders()[0].slot).toBe(0);
});

test("focusNext walks the slots in order, a placeholder's among them", () => {
  const { be, project } = newProject();
  expect(project.loadBytes(zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER, gb("/roms/b.gb")], {}), "").kind).toBe("loaded");
  const ids = project.systems.slotView().map((v) => v.id);
  expect(project.systems.slotView().findIndex((v) => v.focused)).toBe(0);
  const seen: number[] = [];
  for (let i = 0; i < 3; i++) {
    expect(project.systems.focusNext(1)).toBe(true);
    seen.push(project.systems.slotView().find((v) => v.focused)!.id);
  }
  expect(seen).toEqual([ids[1], ids[2], ids[0]]);
});

test("removing the last live system focuses the placeholder", () => {
  const { be, project } = newProject();
  expect(project.loadBytes(zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER], {}), "").kind).toBe("loaded");
  const [live, held] = project.systems.slotView();
  expect(project.systems.removeSystem(live.id)).toBe(true);
  expect(project.systems.slotView()[0].focused).toBe(true);
  expect(project.systems.slotView()[0].id).toBe(held.id);
});

test("a project holding only a placeholder is not pristine: a settings edit dirties it", () => {
  const { be, project } = newProject();
  expect(project.loadBytes(zipOf(be, [PLACEHOLDER], {}), "").kind).toBe("loaded");
  expect(project.isDirty()).toBe(false);
  expect(project.setZoom(3)).toBe(true);
  expect(project.isDirty()).toBe(true);
});
