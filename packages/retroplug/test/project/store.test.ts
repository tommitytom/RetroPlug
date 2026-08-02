// ProjectStore: top-level project state — settings, currentPath, dirty, and
// new/save/load over the REAL systems + recent stores. The deep test is the
// round-trip: save a thin raw-JSON .rplg, reload it, and confirm the systems rebuild
// (through the actual systems store over seeded ROMs), including missing-file relink.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { K_PROJECT } from "../../src/projectConfig";
import type { SystemLayout } from "../../src/settingsEnums";
import { gbRom, gbRomBattery } from "../systems/fixtures";

function newProject(be = new MockBackend("/cfg")) {
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent);
  return { be, recent, project };
}

test("new: clears systems, settings, path and dirty", () => {
  const { be, project } = newProject();
  be.seed("/proj/a.gb", gbRom());
  project.systems.addSystem("/proj/a.gb");
  project.setLayout("grid");
  expect(project.isDirty()).toBeTruthy();

  project.newProject();
  expect(project.systems.view().length).toBe(0);
  expect(project.settings().layout).toBe("auto");
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

test("adoptRomProject: a fresh ROM open writes the sibling .rplg and enters recents", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const r = project.systems.loadRom("/roms/a.gb"); // no /roms/a.rplg sibling → builds a bare system
  expect(r && "system" in r).toBeTruthy();

  project.adoptRomProject("/roms/a.gb");

  const onDisk = be.readText("/roms/a.rplg")!;
  expect(onDisk[0]).toBe("{"); // thin raw JSON, not a "PK" zip
  expect(JSON.parse(onDisk).systems[0].romPath).toBe("a.gb"); // rebased relative to the .rplg folder
  expect(JSON.parse(onDisk).name).toBe(undefined); // no name on the file - the project is unnamed
  expect(recent.view().map((v) => v.path)).toEqual(["/roms/a.rplg"]);
  expect(recent.view()[0].label).toBe("a"); // recents shows the DERIVED name (rom stem), not "a.rplg"
  expect(project.name()).toBe(""); // nothing typed under Project > Name
  expect(project.currentPath()).toBe("/roms/a.rplg");
  expect(project.isDirty()).toBeFalsy(); // the on-disk sibling matches the load
});

test("project name: blank by default - the display name is derived from the systems, never written to the .rplg", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  project.systems.loadRom("/roms/a.gb");
  project.adoptRomProject("/roms/a.gb"); // writes /roms/a.rplg

  expect(project.name()).toBe(""); // the project has no name of its own
  expect(project.displayName()).toBe("a"); // derived from the sole system (its rom stem)
  expect(JSON.parse(be.readText("/roms/a.rplg")!).name).toBe(undefined); // NOT persisted
  expect(recent.view()[0].label).toBe("a"); // the recents entry shows the derived name

  project.save("/roms/copy.rplg"); // Save-As: still nameless on disk, still labelled by the system
  expect(JSON.parse(be.readText("/roms/copy.rplg")!).name).toBe(undefined);
  expect(recent.view()[0].label).toBe("a");
});

test("project name: the derived display name follows the primary system (a paired sav wins over the rom)", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/lsdj.gb", gbRom());
  be.seed("/saves/mysong.sav", "battery");
  project.systems.loadRom("/roms/lsdj.gb", { explicitSav: "/saves/mysong.sav" }); // paired override
  project.adoptRomProject("/roms/lsdj.gb");
  expect(project.displayName()).toBe("mysong"); // the sav stem, not "lsdj"
  // The recents entry names the cart in full: the loaded sav (with its extension), then the ROM.
  expect(recent.view()[0].label).toBe("mysong.sav - lsdj");
});

test("recents name: a battery cart's own sibling sav collapses into the ROM name; a suffixed one doesn't", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/a.gb", gbRomBattery());
  project.systems.addSystem("/roms/a.gb"); // suffix 0 → /roms/a.sav, the ROM's own sibling
  project.save("/roms/a.rplg");
  expect(recent.view()[0].label).toBe("a"); // "a - a" collapsed to one segment

  // A second instance of the same ROM takes /roms/a-2.sav — a distinct file, so it earns its own segment.
  const id = project.systems.addSystem("/roms/a.gb")!;
  project.systems.setFocus(id);
  project.save("/roms/two.rplg");
  expect(recent.view()[0].label).toBe("a-2.sav - a");
});

test("recents name: the sav segment carries its own extension, so a .srm reads as one", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/lsdj.gb", gbRom());
  be.seed("/saves/other.srm", "battery");
  project.systems.addSystem("/roms/lsdj.gb", { explicitSav: "/saves/other.srm" });
  project.save("/proj/p.rplg");
  expect(recent.view()[0].label).toBe("other.srm - lsdj");
});

test("recents name: a battery-less cart names the ROM alone; the project's own name replaces both", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/game.gb", gbRom()); // no battery, no sav to speak of
  project.systems.addSystem("/roms/game.gb");
  project.save("/roms/game.rplg");
  expect(recent.view()[0].label).toBe("game");

  project.setName("My Song"); // a named project shows THAT instead of the sav / ROM pair
  project.save("/roms/game.rplg");
  expect(recent.view()[0].label).toBe("My Song");
});

test("setName: names the project, persists on save, restores on load, and clears back to derived", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  project.systems.loadRom("/roms/a.gb");
  project.adoptRomProject("/roms/a.gb");

  expect(project.setName("  My Song  ")).toBeTruthy(); // trimmed
  expect(project.name()).toBe("My Song");
  expect(project.displayName()).toBe("My Song"); // the user's name beats the derived one
  expect(project.setName("My Song")).toBeFalsy(); // unchanged → no-op
  expect(project.isDirty()).toBeTruthy(); // needs a save to persist

  project.save("/roms/a.rplg");
  expect(JSON.parse(be.readText("/roms/a.rplg")!).name).toBe("My Song"); // NOW it's on the .rplg
  expect(recent.view()[0].label).toBe("My Song");

  project.newProject();
  expect(project.name()).toBe(""); // torn down
  project.load("/roms/a.rplg");
  expect(project.name()).toBe("My Song"); // restored from the file

  expect(project.setName("   ")).toBeTruthy(); // blank clears it
  expect(project.name()).toBe("");
  expect(project.displayName()).toBe("a"); // back to the derived name
  project.save("/roms/a.rplg");
  expect(JSON.parse(be.readText("/roms/a.rplg")!).name).toBe(undefined); // and off the file again
  expect(recent.view()[0].label).toBe("a");
});

test("adoptRomProject: an existing sibling .rplg is tracked, never overwritten, and stays dirty", () => {
  const { be, recent, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const sentinel = '{"schemaVersion":"1","authored":true}';
  be.seed("/roms/a.rplg", sentinel); // a project the user already saved beside the ROM

  // A paired-sav load bypasses loadRom's auto-defer, so we reach adopt with the sibling present + dirty.
  project.systems.loadRom("/roms/a.gb", { explicitSav: "/roms/b.sav" });
  expect(project.isDirty()).toBeTruthy();
  project.adoptRomProject("/roms/a.gb");

  expect(be.readText("/roms/a.rplg")).toBe(sentinel); // untouched — the user's project is preserved
  expect(recent.view().map((v) => v.path)).toEqual(["/roms/a.rplg"]); // still tracked
  expect(project.currentPath()).toBe("/roms/a.rplg");
  expect(project.isDirty()).toBeTruthy(); // the pinned paired-sav override survives to the next save
});

test("adoptRomProject: an embedded ROM (no path) is a no-op — nothing written or tracked", () => {
  const { be, recent, project } = newProject();
  project.systems.loadMgb(); // embedded mGB: no on-disk sibling
  project.adoptRomProject("");
  expect(recent.view().length).toBe(0);
  expect(be.log.includes("writeFile")).toBeFalsy();
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
  be.seed("/proj/x.rplg", JSON.stringify({ schemaVersion: String(K_PROJECT + 1), settings: {}, systems: [] }));
  expect(project.load("/proj/x.rplg")).toEqual({ kind: "incompatible" });
});

test("load: a PK archive under a .rplg name errors — a .rplg must be pure JSON, never a zip", () => {
  const { be, project } = newProject();
  // A `.rplg` whose bytes are a PKZIP (the reverted zip-as-.rplg design). Routing is by extension, so a
  // `.rplg` is parsed as JSON — PK magic isn't valid JSON → error, and it is NEVER unzipped or silently
  // coerced to an empty project. (Zip projects must use `.rplg.zip`.)
  be.seed("/proj/z.rplg", new Uint8Array([0x50, 0x4b, 0x03, 0x04])); // "PK\x03\x04"
  expect(project.load("/proj/z.rplg")).toEqual({ kind: "error" });
  expect(be.log.includes("unzip")).toBeFalsy(); // took the thin JSON path, not the zip path
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
  expect(project.setLayout("bogus" as SystemLayout)).toBeFalsy(); // unknown value → rejected, no change
  expect(project.settings().layout).toBe("auto");
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
