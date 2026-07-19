// resolveDropAction — the drag-and-drop decision table. Given the dropped path(s), the live instance
// count, and which tile (if any) the drop hit, decide what to do: load-as-project, replace an instance,
// load a .sav into one, or pair an orphan .sav. Pure + store-free, so every branch is checked here
// against the mock backend; the coordinate → tile mapping is in hit-test.test.ts, and the App wiring in
// the UI harness.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { resolveDropAction, type DropContext } from "../../src/fileDrop";
import { gbRom } from "../systems/fixtures";

const ROM = "/roms/game.gb";
const SAV = "/roms/game.sav";
const RPLG = "/roms/game.rplg";

function beWith(rom = true, sav = true): MockBackend {
  const be = new MockBackend("/cfg");
  if (rom) be.seed(ROM, gbRom());
  if (sav) be.seed(SAV, "battery");
  return be;
}

// A context with no sibling ROM by default; tests override the fields they exercise.
function ctx(over: Partial<DropContext>): DropContext {
  return { count: 0, targetId: null, onTile: false, targetIsLsdj: false, siblingRom: () => null, ...over };
}

test("ROM on the start screen → loadRom (new project)", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 0 }), [ROM])).toEqual({ type: "loadRom", romPath: ROM });
});

test("ROM on a single instance → loadRom (new project, target ignored)", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 1, targetId: 5, onTile: true }), [ROM])).toEqual({ type: "loadRom", romPath: ROM });
});

test("ROM on a multi-instance project + tile hit → replace that tile", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 3, targetId: 7, onTile: true }), [ROM])).toEqual({ type: "replace", id: 7, romPath: ROM });
});

test("ROM on a multi-instance project, missed tile → replace the focused instance", () => {
  const be = beWith();
  // targetId is the focus fallback (onTile false); a ROM still replaces it.
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 3, onTile: false }), [ROM])).toEqual({ type: "replace", id: 3, romPath: ROM });
});

test("ROM with a sibling <rom>.rplg (start screen) → loadProject that sibling, not a fresh ROM", () => {
  const be = beWith();
  be.seed(RPLG, "{}");
  expect(resolveDropAction(be, ctx({ count: 0 }), [ROM])).toEqual({ type: "loadProject", path: RPLG });
});

test("ROM with a sibling <rom>.rplg on a single instance → loadProject that sibling", () => {
  const be = beWith();
  be.seed(RPLG, "{}");
  expect(resolveDropAction(be, ctx({ count: 1, targetId: 5, onTile: true }), [ROM])).toEqual({ type: "loadProject", path: RPLG });
});

test("ROM + paired .sav ignores a sibling <rom>.rplg → fresh ROM with the paired sav", () => {
  const be = beWith();
  be.seed(RPLG, "{}");
  // A paired save means "boot the ROM with this save", not "open the sibling project" (mirrors resolveLoad).
  expect(resolveDropAction(be, ctx({ count: 0 }), [ROM, SAV])).toEqual({ type: "loadRom", romPath: ROM, explicitSav: SAV });
});

test("ROM with a sibling <rom>.rplg dropped onto a multi-instance tile → replace, not the sibling project", () => {
  const be = beWith();
  be.seed(RPLG, "{}");
  // Replace-in-place stays in the current project — the sibling defer only applies to a new-project load.
  expect(resolveDropAction(be, ctx({ count: 3, targetId: 7, onTile: true }), [ROM])).toEqual({ type: "replace", id: 7, romPath: ROM });
});

test("ROM + its .sav dropped together → paired explicitSav", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 0 }), [ROM, SAV])).toEqual({ type: "loadRom", romPath: ROM, explicitSav: SAV });
});

test("ROM + .sav dropped onto a multi-instance tile → replace with the paired sav", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 4, onTile: true }), [ROM, SAV])).toEqual({ type: "replace", id: 4, romPath: ROM, explicitSav: SAV });
});

test("bare .sav with a sibling ROM (start/single) → loadRom with explicitSav", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 0, siblingRom: () => ROM }), [SAV])).toEqual({ type: "loadRom", romPath: ROM, explicitSav: SAV });
});

test("bare .sav with no sibling ROM → pairSav (App asks for a ROM)", () => {
  const be = beWith(false, true);
  expect(resolveDropAction(be, ctx({ count: 1, siblingRom: () => null }), [SAV])).toEqual({ type: "pairSav", sav: SAV });
});

test("bare .sav dropped onto a multi-instance tile → loadSram into that instance", () => {
  const be = beWith(false, true);
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 9, onTile: true }), [SAV])).toEqual({ type: "loadSram", id: 9, sav: SAV });
});

test("bare .sav in a multi-instance project but missed every tile → treated as a load", () => {
  const be = beWith();
  // onTile false → not a loadSram; falls through to sibling pairing.
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 1, onTile: false, siblingRom: () => ROM }), [SAV])).toEqual({ type: "loadRom", romPath: ROM, explicitSav: SAV });
});

test("a project file always loads as a project, even in a multi-instance project", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 3, targetId: 2, onTile: true }), ["/p/song.rplg"])).toEqual({ type: "loadProject", path: "/p/song.rplg" });
  expect(resolveDropAction(be, ctx({ count: 3, targetId: 2, onTile: true }), ["/p/song.rplg.zip"])).toEqual({ type: "loadProject", path: "/p/song.rplg.zip" });
});

test("a project file wins over a co-dropped ROM", () => {
  const be = beWith();
  expect(resolveDropAction(be, ctx({ count: 0 }), [ROM, "/p/song.rplg"])).toEqual({ type: "loadProject", path: "/p/song.rplg" });
});

test("an unsupported file → ignore", () => {
  const be = new MockBackend("/cfg");
  be.seed("/x/notes.txt", "hello");
  expect(resolveDropAction(be, ctx({ count: 0 }), ["/x/notes.txt"])).toEqual({ type: "ignore", reason: "unsupported file" });
});

test("no paths → ignore", () => {
  expect(resolveDropAction(new MockBackend("/cfg"), ctx({ count: 0 }), [])).toEqual({ type: "ignore", reason: "no files" });
});

test("siblingRom wired through the real SystemsStore.resolveSiblingRom pairs a dropped .sav", () => {
  const be = beWith();
  const systems = new SystemsStore(be);
  const out = resolveDropAction(be, ctx({ count: 0, siblingRom: (s) => systems.resolveSiblingRom(s) }), [SAV]);
  expect(out).toEqual({ type: "loadRom", romPath: ROM, explicitSav: SAV });
});

// --- song files (.lsdsng / .lsdprj) patch into an LSDj instance ---
test("song files onto an LSDj instance → patchSongs with just the song paths", () => {
  const be = new MockBackend("/cfg");
  const out = resolveDropAction(be, ctx({ count: 2, targetId: 4, onTile: true, targetIsLsdj: true }), ["/s/a.lsdsng", "/s/b.lsdprj"]);
  expect(out).toEqual({ type: "patchSongs", id: 4, paths: ["/s/a.lsdsng", "/s/b.lsdprj"] });
});

test("a mixed song + ROM drop onto an LSDj instance → patchSongs (ROM ignored)", () => {
  const be = beWith();
  const out = resolveDropAction(be, ctx({ count: 2, targetId: 4, onTile: true, targetIsLsdj: true }), [ROM, "/s/a.lsdsng"]);
  expect(out).toEqual({ type: "patchSongs", id: 4, paths: ["/s/a.lsdsng"] });
});

test("song files onto a non-LSDj target → ignore", () => {
  const be = new MockBackend("/cfg");
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 4, onTile: true, targetIsLsdj: false }), ["/s/a.lsdsng"])).toEqual({
    type: "ignore",
    reason: "song files need an LSDj instance",
  });
});

test("song files on the start screen (no instance) → ignore", () => {
  const be = new MockBackend("/cfg");
  expect(resolveDropAction(be, ctx({ count: 0, targetIsLsdj: false }), ["/s/a.lsdprj"])).toEqual({
    type: "ignore",
    reason: "song files need an LSDj instance",
  });
});

test("a project file still wins over song files", () => {
  const be = new MockBackend("/cfg");
  expect(resolveDropAction(be, ctx({ count: 2, targetId: 4, targetIsLsdj: true }), ["/s/a.lsdsng", "/p/x.rplg"])).toEqual({
    type: "loadProject",
    path: "/p/x.rplg",
  });
});
