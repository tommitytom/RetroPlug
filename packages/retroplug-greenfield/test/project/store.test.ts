// ProjectStore: top-level project state — settings, currentPath, dirty, and
// new/save/load over the REAL systems + recent stores. The deep test is the
// round-trip: save a thin raw-JSON .rplg, reload it, and confirm the systems rebuild
// (through the actual systems store over seeded ROMs), including missing-file relink.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { gbRom } from "../systems/fixtures";

function newProject(be = new MockBackend("/cfg")) {
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent);
  return { be, recent, project };
}

test("new: clears systems, settings, path and dirty", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  project.systems.addSystem("/proj/a.gb");
  project.setLayout(3);
  expect(project.isDirty()).toBeTruthy();

  project.newProject();
  expect(project.systems.view().length).toBe(0);
  expect(project.settings().layout).toBe(0);
  expect(project.currentPath()).toBe("");
  expect(project.isDirty()).toBeFalsy();
});

test("save: writes raw JSON (not a zip), records recents + path, clears dirty", () => {
  const { be, recent, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  project.systems.addSystem("/proj/a.gb");
  expect(project.save("/proj/song.rplg")).toBeTruthy();

  const onDisk = be.readText("/proj/song.rplg")!;
  expect(onDisk[0]).toBe("{"); // raw JSON, not a "PK" zip
  const doc = JSON.parse(onDisk);
  expect(doc.systems[0].romPath).toBe("a.gb"); // rebased relative to the .rplg folder
  expect(recent.view().map((v) => v.path)).toEqual(["/proj/song.rplg"]);
  expect(project.currentPath()).toBe("/proj/song.rplg");
  expect(project.isDirty()).toBeFalsy();
});

test("save then load: round-trips the systems (rebuilt over the real store)", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  project.systems.addSystem("/proj/a.gb"); // suffix 0
  project.systems.addSystem("/proj/a.gb"); // suffix 2 (disambiguated)
  project.save("/proj/song.rplg");

  project.newProject(); // tear everything down
  expect(project.systems.view().length).toBe(0);

  const out = project.load("/proj/song.rplg");
  expect(out).toEqual({ kind: "loaded", systems: 2 });
  const v = project.systems.view();
  expect(v.map((s) => s.romPath)).toEqual(["/proj/a.gb", "/proj/a.gb"]); // restored absolute
  expect(v.map((s) => s.savSuffix)).toEqual([0, 2]); // identity preserved
  expect(project.currentPath()).toBe("/proj/song.rplg");
  expect(project.isDirty()).toBeFalsy();
});

test("load: a moved ROM reports missing, then relink completes the load", () => {
  // Author a .rplg (relative romPath a.gb) via a save, then load it on a fresh disk
  // where the ROM lives at /new instead of beside the project.
  const author = newProject();
  author.be.seed("/proj/a.gb", gbRom());
  author.project.systems.addSystem("/proj/a.gb");
  author.project.save("/proj/song.rplg");
  const rplg = author.be.readText("/proj/song.rplg")!;

  const { be, project } = newProject();
  be.seed("/proj/song.rplg", rplg); // the project, but no /proj/a.gb on this disk
  be.seed("/new/a.gb", gbRom()); // the ROM moved here

  const first = project.load("/proj/song.rplg");
  expect(first.kind).toBe("missing");
  expect((first as { missing: unknown[] }).missing).toEqual([
    { systemIndex: 0, itemKind: "rom", path: "/proj/a.gb" },
  ]);
  expect(project.systems.view().length).toBe(0); // nothing built yet

  const done = project.relink({ systemIndex: 0, itemKind: "rom", path: "/proj/a.gb" }, "/new/a.gb");
  expect(done).toEqual({ kind: "loaded", systems: 1 });
  expect(project.systems.view()[0].romPath).toBe("/new/a.gb");
});

test("load: a project stamped newer than this build is incompatible", () => {
  const { be, project } = newProject();
  be.seed("/proj/x.rplg", JSON.stringify({ schemaVersion: "2", settings: {}, systems: [] }));
  expect(project.load("/proj/x.rplg")).toEqual({ kind: "incompatible" });
});

test("load: a PK archive routes through the import path; no project.json is corrupt", () => {
  const { be, project } = newProject();
  // A bare PK magic with no entries → valid zip, but no project.json → corrupt archive.
  be.seed("/proj/z.rplg", new Uint8Array([0x50, 0x4b, 0x03, 0x04])); // "PK\x03\x04"
  expect(project.load("/proj/z.rplg")).toEqual({ kind: "error" });
  expect(be.log.includes("unzip")).toBeTruthy(); // took the export import path, not thin
});

test("dirty: flips on a systems mutation or a settings change; clears on save/new", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  expect(project.isDirty()).toBeFalsy();
  project.systems.addSystem("/proj/a.gb"); // systems onChange → dirty
  expect(project.isDirty()).toBeTruthy();
  project.save("/proj/song.rplg");
  expect(project.isDirty()).toBeFalsy();
  expect(project.setZoom(4)).toBeTruthy(); // settings change → dirty
  expect(project.isDirty()).toBeTruthy();
  expect(project.setLayout(99)).toBeFalsy(); // out of range → rejected, no change
  expect(project.settings().layout).toBe(0);
});

test("setOnChange: the general observer fires on settings edits, save, and new", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  let changes = 0;
  project.setOnChange(() => changes++);

  expect(project.setZoom(4)).toBeTruthy(); // a silent settings setter — now observed
  expect(changes).toBe(1);
  project.systems.addSystem("/proj/a.gb"); // structural edit marks dirty → onChange too
  expect(changes).toBe(2);
  project.save("/proj/song.rplg"); // dirty-clearing transition
  expect(changes).toBe(3);
  project.newProject();
  expect(changes).toBe(4);
});

test("setFocus: fires the focus observer for a re-render but does NOT mark the project dirty", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  project.systems.addSystem("/proj/a.gb"); // first → focused
  const b = project.systems.addSystem("/proj/a.gb")!; // second → not focused
  project.save("/proj/song.rplg"); // clean baseline (adds dirtied)
  expect(project.isDirty()).toBeFalsy();

  let focusNotifies = 0;
  project.systems.setOnFocusChange(() => focusNotifies++);

  expect(project.systems.setFocus(b)).toBeTruthy();
  expect(project.systems.focused()).toBe(b);
  expect(focusNotifies).toBe(1);
  expect(project.isDirty()).toBeFalsy(); // focus is transient — not a project edit

  expect(project.systems.setFocus(b)).toBeFalsy(); // already focused → no-op, no notify
  expect(project.systems.setFocus(999999)).toBeFalsy(); // unknown id → no-op, no notify
  expect(focusNotifies).toBe(1);
});
