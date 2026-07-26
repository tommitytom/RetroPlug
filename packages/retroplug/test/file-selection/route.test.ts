// FileSelection classification + the resolve-only "Load…" decision: classifyKind
// (rom / sav / other) and resolveLoad — which branches a pick into a fresh ROM vs a
// sibling <rom>.rplg project, running the .sav→ROM pairing. resolveLoad NEVER mutates
// the store (the caller applies the guarded reset+load). The async 2nd-browser + the
// add/replace mutators live in browse.test.ts.
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

test("classifyKind: ROM by content, .sav/.srm by extension, else other", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  expect(classifyKind(be, "/roms/a.gb")).toBe("rom");
  be.seed("/roms/save.sav", "battery");
  expect(classifyKind(be, "/roms/save.sav")).toBe("sav"); // not a ROM, .sav extension
  be.seed("/roms/save.srm", "battery");
  expect(classifyKind(be, "/roms/save.srm")).toBe("sav"); // some NES/risa saves use .srm
  be.seed("/roms/notes.txt", "hello");
  expect(classifyKind(be, "/roms/notes.txt")).toBe("other");
  expect(classifyKind(be, "/roms/missing.gb")).toBe("other"); // absent → not a ROM, not .sav
});

test("resolveLoad: a fresh ROM resolves to the rom branch and mutates nothing", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.queueBrowse("/roms/a.gb");
  const out = await fs.resolveLoad();
  expect(out).toEqual({ kind: "rom", romPath: "/roms/a.gb", explicitSav: undefined });
  expect(systems.view().length).toBe(0); // resolve-only — no build
});

test("resolveLoad: a ROM beside a <rom>.rplg resolves to the project branch", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.rplg", "project bytes");
  be.queueBrowse("/roms/a.gb");
  const out = await fs.resolveLoad();
  expect(out).toEqual({ kind: "project", path: "/roms/a.rplg" });
});

test("resolveLoad: a .sav beside its sibling ROM pairs immediately (no 2nd browser)", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/game.gb", gbRom());
  be.seed("/roms/game.sav", "battery");
  be.queueBrowse("/roms/game.sav");
  const out = await fs.resolveLoad();
  expect(out).toEqual({ kind: "rom", romPath: "/roms/game.gb", explicitSav: "/roms/game.sav" });
  expect(be.fileBrowserCalls.length).toBe(1); // sibling found → no 2nd browser
});

test("resolveLoad: a paired .sav pins the ROM branch even beside a <rom>.rplg", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/game.gb", gbRom());
  be.seed("/roms/game.rplg", "project bytes"); // a sibling project exists…
  be.seed("/roms/game.sav", "battery");
  be.queueBrowse("/roms/game.sav");
  const out = await fs.resolveLoad();
  // …but a picked save pins the pairing, so it opens as a ROM, not the project.
  expect(out).toEqual({ kind: "rom", romPath: "/roms/game.gb", explicitSav: "/roms/game.sav" });
});

test("resolveLoad: an 'other' file errors", async () => {
  const { be, fs } = newFs();
  be.seed("/roms/notes.txt", "hello");
  be.queueBrowse("/roms/notes.txt");
  const out = await fs.resolveLoad();
  expect(out).toEqual({ kind: "error", path: "/roms/notes.txt" });
});

test("resolveLoad: a cancelled dialog resolves to cancelled", async () => {
  const { fs, be } = newFs();
  be.queueBrowse(null);
  const out = await fs.resolveLoad();
  expect(out).toEqual({ kind: "cancelled" });
});
