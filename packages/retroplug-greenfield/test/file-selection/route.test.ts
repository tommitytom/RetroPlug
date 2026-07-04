// FileSelection routing without a dialog: classifyKind (rom / sav / other) and
// selectPath — the classify-and-route core shared by browse() and the no-dialog
// entries (autoload / recent / drag-drop). A ROM loads or adds; a .sav pairs with its
// sibling ROM (pinning an override only when the pick differs); a ROM beside a
// <rom>.rplg defers to the project. Port of handleOpenRomSelection's content routing.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { FileSelection, classifyKind } from "../../src/fileSelection";
import { gbRom } from "../systems/fixtures";

function newFs() {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be);
  const fs = new FileSelection(be, systems);
  return { be, systems, fs };
}

test("classifyKind: ROM by content, .sav by extension, else other", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  expect(classifyKind(be, "/roms/a.gb")).toBe("rom");
  be.seed("/roms/save.sav", "battery");
  expect(classifyKind(be, "/roms/save.sav")).toBe("sav"); // not a ROM, .sav extension
  be.seed("/roms/notes.txt", "hello");
  expect(classifyKind(be, "/roms/notes.txt")).toBe("other");
  expect(classifyKind(be, "/roms/missing.gb")).toBe("other"); // absent → not a ROM, not .sav
});

test("selectPath: a ROM loads (and adds under the add mode)", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  const out = await fs.selectPath("/roms/a.gb", "load");
  expect(out).toEqual({ kind: "loaded", system: systems.focused() });
  expect(systems.view().length).toBe(1);

  be.seed("/roms/b.gb", gbRom());
  const add = await fs.selectPath("/roms/b.gb", "add");
  expect(add.kind).toBe("added");
  expect(systems.view().length).toBe(2);
});

test("selectPath: a .sav beside its sibling ROM pairs immediately (no dialog)", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/game.gb", gbRom());
  be.seed("/roms/game.sav", "battery");
  const out = await fs.selectPath("/roms/game.sav", "load");
  expect(out.kind).toBe("loaded");
  expect(be.fileBrowserCalls.length).toBe(0); // sibling found → no 2nd browser
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romPath).toBe("/roms/game.gb"); // paired with the sibling ROM
  expect(spec.savPath).toBe("/roms/game.sav"); // == the sibling → resolves to it (no override)
});

test("selectPath: a .sav whose file differs from the sibling pins it as an override", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/game.gb", gbRom());
  be.seed("/saves/mine.sav", "battery"); // a different-dir save, but named to pair via stem? no — pick directly
  // Pair a save that lives elsewhere with game.gb by picking game's sibling rom first:
  // here the .sav has no sibling rom of its own, so it would open a 2nd browser. To
  // exercise the override path deterministically, drive selectPath on the ROM with the
  // save handed in is not how selectPath works; instead pick the sibling-less sav and
  // answer the 2nd browser — covered in browse.test.ts. This case asserts the override
  // resolution via a sibling-paired sav that is NOT the rom's natural sibling name.
  be.seed("/roms/game-2.sav", "battery"); // sibling by stem "game-2" -> base "game"
  const out = await fs.selectPath("/roms/game-2.sav", "load");
  expect(out.kind).toBe("loaded");
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romPath).toBe("/roms/game.gb"); // base-stem sibling
  expect(spec.savPath).toBe("/roms/game-2.sav"); // a different file than game.sav → pinned override
});

test("selectPath: an 'other' file errors", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/notes.txt", "hello");
  const out = await fs.selectPath("/roms/notes.txt", "load");
  expect(out).toEqual({ kind: "error", path: "/roms/notes.txt" });
});

test("selectPath: a ROM beside a <rom>.rplg defers to the project", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.rplg", "project bytes");
  const out = await fs.selectPath("/roms/a.gb", "load");
  expect(out).toEqual({ kind: "deferred", project: "/roms/a.rplg" });
});
