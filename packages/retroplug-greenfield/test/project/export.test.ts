// ProjectStore export/import: the pump→zip round-trip. export gathers each live
// system's SRAM/savestate from the pump (backend.read*) and frames a PKZIP `.rplg`
// (native only compresses); load routes a PK archive back through the same
// scan/relink tail, its blobs seeding each reconstructed emulator. Path-backed ROMs
// stay referenced by path (not embedded); embedded ROMs reconstruct from their marker.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor, sramBytesFor } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { gbRom } from "../systems/fixtures";

const dec = new TextDecoder();

function newProject(be = new MockBackend("/cfg")) {
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent);
  return { be, recent, project };
}

test("export: PKZIP of project.json + each system's state & sram, from the pump", () => {
  const { be, recent, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  const mgbId = project.systems.loadMgb()!; // embedded, appended into empty → id 1
  const gbId = project.systems.addSystem("/proj/a.gb")!; // path-backed → id 2
  project.setLayout(3);

  expect(project.export("/proj/song.rplg")).toBeTruthy();

  // Written atomically, PK-prefixed.
  const onDisk = be.readFile("/proj/song.rplg")!;
  expect([onDisk[0], onDisk[1], onDisk[2], onDisk[3]]).toEqual([0x50, 0x4b, 0x03, 0x04]);
  expect(be.log.includes("writeFileAtomic")).toBeTruthy();

  // The exact entry set handed to the native compressor.
  const entries = be.zipCalls[be.zipCalls.length - 1];
  expect(entries.map((e) => e.name).sort()).toEqual([
    "project.json",
    "systems/0/sram",
    "systems/0/state",
    "systems/1/sram",
    "systems/1/state",
  ]);

  // project.json re-parses to the thin config (systems in order, ROM by path/marker).
  const cfgEntry = entries.find((e) => e.name === "project.json")!;
  const doc = JSON.parse(dec.decode(cfgEntry.bytes));
  expect(doc.settings.layout).toBe(3);
  expect(doc.systems.length).toBe(2);
  expect(doc.systems[0].embeddedRom).toBe("mgb");
  expect(doc.systems[1].romPath).toBe("a.gb"); // rebased relative, NOT embedded as a blob

  // Blobs are the deterministic pump bytes, keyed by config index (i, not id).
  const blob = (name: string) => entries.find((e) => e.name === name)!.bytes;
  expect(blob("systems/0/state")).toEqual(stateBytesFor(mgbId));
  expect(blob("systems/0/sram")).toEqual(sramBytesFor(mgbId));
  expect(blob("systems/1/state")).toEqual(stateBytesFor(gbId));
  expect(blob("systems/1/sram")).toEqual(sramBytesFor(gbId));

  expect(recent.view().map((v) => v.path)).toEqual(["/proj/song.rplg"]);
  expect(project.currentPath()).toBe("/proj/song.rplg");
  expect(project.isDirty()).toBeFalsy();
});

test("export then load: round-trips systems (blobs seed each emulator) + settings", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  const mgbId = project.systems.loadMgb()!; // export id (mgb) — TS-allocated, not assumed to be 1
  const aId = project.systems.addSystem("/proj/a.gb")!; // export id (a.gb)
  project.setZoom(4);
  project.export("/proj/song.rplg");

  project.newProject();
  expect(project.systems.view().length).toBe(0);

  const out = project.load("/proj/song.rplg");
  expect(out).toEqual({ kind: "loaded", systems: 2 });
  const v = project.systems.view();
  expect(v.map((s) => s.embedded)).toEqual([true, false]); // mgb, then a.gb
  expect(v.map((s) => s.romPath)).toEqual(["", "/proj/a.gb"]); // absolute again
  expect(project.settings().zoom).toBe(4);

  // Both new systems were reconstructed FROM the archive's blobs (not cold-booted).
  expect(be.restoredIds()).toEqual(project.systems.systems().map((s) => s.id).sort((a, b) => a - b));
  // The exact archive bytes (keyed by the EXPORT-time ids) reached construct.
  const imported = be.constructCalls.slice(-2);
  expect(new Uint8Array(imported[0].stateBytes!)).toEqual(stateBytesFor(mgbId));
  expect(new Uint8Array(imported[1].stateBytes!)).toEqual(stateBytesFor(aId));
});

test("load export: a moved cartridge ROM reports missing (blobs cover only sram/state)", () => {
  // Author an export on one disk, then load it on a disk missing the cartridge ROM.
  const author = newProject();
  author.be.seed("/proj/a.gb", gbRom());
  author.project.systems.addSystem("/proj/a.gb");
  author.project.export("/proj/song.rplg");
  const archive = author.be.readFile("/proj/song.rplg")!;

  const { be, project } = newProject();
  be.seed("/proj/song.rplg", archive); // the export, but no /proj/a.gb on this disk

  const first = project.load("/proj/song.rplg");
  expect(first.kind).toBe("missing"); // the ROM isn't in the zip — only the save is
  expect((first as { missing: unknown[] }).missing).toEqual([
    { systemIndex: 0, itemKind: "rom", path: "/proj/a.gb" },
  ]);

  be.seed("/new/a.gb", gbRom()); // the ROM moved here
  const done = project.relink({ systemIndex: 0, itemKind: "rom", path: "/proj/a.gb" }, "/new/a.gb");
  expect(done).toEqual({ kind: "loaded", systems: 1 });
  expect(project.systems.view()[0].romPath).toBe("/new/a.gb");
  expect(be.restoredIds().length).toBe(1); // still restored from the archive after relink
});
