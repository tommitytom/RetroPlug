// The Songs menu's "import from a .sav" feature: validate a source save against the loaded cart's console,
// then a checkbox picker imports the chosen songs. Covers the console-agnostic hook helpers (planImport /
// buildImportModal / applyImport in ui/lvgl/songImport) plus the per-console SongCatalog validation +
// subset import, driven through the composed stores like the other menu tests.
//
// The write cycle (applyImport) is asserted against the last constructCall's sramBytes — the bytes handed
// to the rebuild — because MockBackend doesn't round-trip a loadSram back through readSram (see
// lsdj-songs.test.ts, which asserts the same way).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { planImport, buildImportModal, applyImport, type PickState, type ImportPending } from "../../ui/lvgl/songImport";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import type { SystemView } from "../../src/systemsStore";
import { savFrom, injectSong, listProjects, isLsdjSav } from "../../src/lsdjSav";
import { sameBytes } from "../_bytes";
import { ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { gbRomBattery, risaRom } from "../systems/fixtures";
import { savBytes } from "../risa/fixtures";
import { normalizeSaveContainer, listSongs, isRisaSav } from "../../src/risaSav";

const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
const noop = () => {};
const handlers = { toggle: noop, toggleAll: noop, apply: noop, onClose: noop };
const lastSram = (be: MockBackend): Uint8Array => be.constructCalls[be.constructCalls.length - 1].sramBytes!;

// --- fixtures ------------------------------------------------------------------------------------------

// A full 1 MiB LSDj ROM (title "LSDJ…" → the provider attaches lsdj-sync). Mirrors lsdj-songs.test.ts.
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  const count = 2;
  b.set(PALETTE_CHECK, 1 * BANK_SIZE + 0x100 + count * PALETTE_SIZE);
  const nb = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3 + 2 * count; i++) for (let j = 0; j < 4; j++) b[nb + i * 5 + j] = 0x41 + j;
  b[nb + 15 + 2 * count * 5 + 4] = 0x01;
  return b;
}

// A source LSDj sav carrying three named songs at slots 0,1,2.
function lsdjSource(): Uint8Array {
  const song = savFrom({}).subarray(0, 0x8000).slice();
  let s = savFrom({});
  s = injectSong(s, 0, "AAA", 1, song)!;
  s = injectSong(s, 1, "BBB", 2, song)!;
  s = injectSong(s, 2, "CCC", 3, song)!;
  return s;
}

const RISA_CATALOG = () => normalizeSaveContainer(savBytes("multi_legacy")).save; // HOU8,HOU,DBZ,DBZ2-F,FUNK0

function lsdjSystem(): { be: MockBackend; stores: AppStores; sys: () => SystemView; id: number } {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({})); // an empty target battery
  return { be, stores, id, sys: () => stores.project.systems.view().find((s) => s.id === id)! };
}

function risaSystem(battery: Uint8Array): { be: MockBackend; stores: AppStores; id: number; sys: () => SystemView } {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.nes", risaRom());
  const id = stores.project.systems.addSystem("/roms/song.nes")!;
  be.setSram(id, battery);
  return { be, stores, id, sys: () => stores.project.systems.view().find((s) => s.id === id)! };
}

// --- validation predicates ------------------------------------------------------------------------------

test("isLsdjSav / isRisaSav accept their own console and reject the other + garbage", () => {
  const lsdj = lsdjSource();
  const risa = RISA_CATALOG();
  expect(isLsdjSav(lsdj)).toBe(true);
  expect(isLsdjSav(risa)).toBe(false);
  expect(isLsdjSav(new Uint8Array(16))).toBe(false);
  expect(isRisaSav(risa)).toBe(true);
  expect(isRisaSav(lsdj)).toBe(false);
  expect(isRisaSav(new Uint8Array(16))).toBe(false);
});

// --- planImport (validate + list) -----------------------------------------------------------------------

test("planImport lists a same-console source's songs (all checked) and rejects a wrong-console save", () => {
  const { sys } = lsdjSystem();
  const ok = planImport(sys(), lsdjSource());
  expect(ok.kind).toBe("pick");
  const pick = ok as PickState;
  expect(pick.songs.map((s) => s.name)).toEqual(["AAA", "BBB", "CCC"]);
  expect([...pick.checked].sort()).toEqual([0, 1, 2]); // every song checked by default

  const bad = planImport(sys(), RISA_CATALOG()); // a risa sav in an LSDj cart
  expect(bad.kind).toBe("notice");
  expect((bad as { body: string }).body).toBe("Not a valid LSDj save.");
});

test("planImport rejects a wrong-console save for risa too, and reports a source with no songs", () => {
  const { sys } = risaSystem(RISA_CATALOG());
  const bad = planImport(sys(), lsdjSource());
  expect(bad.kind).toBe("notice");
  expect((bad as { body: string }).body).toBe("Not a valid Risa save.");

  const empty = planImport(sys(), new Uint8Array(0x10000)); // 64 KB of zeros: valid size, no RSAV catalog
  expect(empty.kind).toBe("notice");
});

// --- buildImportModal (the checkbox tree) ---------------------------------------------------------------

test("buildImportModal renders ASCII checkbox rows, an Import(N) button, and the notice/OK tree", () => {
  const { sys } = lsdjSystem();
  const pick = planImport(sys(), lsdjSource()) as PickState;

  const tree = buildImportModal(pick, handlers);
  expect(tree.title).toBe("Import Songs");
  expect(findItem(tree.items, "import-song-0")?.label).toBe("[x] AAA"); // checked by default
  expect(findItem(tree.items, "import-all")?.label).toBe("Select None"); // all checked → offers "None"
  expect(findItem(tree.items, "import-do")?.label).toBe("Import (3)");
  expect(findItem(tree.items, "import-do")?.disabled).toBeFalsy();

  // None checked → an unchecked row + a disabled Import (0).
  const none: PickState = { ...pick, checked: new Set() };
  const t2 = buildImportModal(none, handlers);
  expect(findItem(t2.items, "import-song-1")?.label).toBe("[ ] BBB");
  expect(findItem(t2.items, "import-all")?.label).toBe("Select All");
  expect(findItem(t2.items, "import-do")?.disabled).toBe(true);

  // A notice → title + body + a single OK.
  const notice: ImportPending = { kind: "notice", title: "Cannot import", body: "Not a valid LSDj save." };
  const nt = buildImportModal(notice, handlers);
  expect(nt.title).toBe("Cannot import");
  expect(findItem(nt.items, "import-notice-ok")?.label).toBe("OK");
});

// --- applyImport (the write cycle) ----------------------------------------------------------------------

test("applyImport writes only the checked LSDj songs into the live battery", () => {
  const { be, stores, sys } = lsdjSystem();
  const pick = planImport(sys(), lsdjSource()) as PickState;
  const subset: PickState = { ...pick, checked: new Set([0, 2]) }; // AAA + CCC, skip BBB

  expect(applyImport(stores, subset)).toEqual({ requested: 2, imported: 2 });
  expect(listProjects(lastSram(be)).map((p) => p.name)).toEqual(["AAA", "CCC"]); // BBB not imported
});

test("applyImport appends only the checked risa songs into the live catalog", () => {
  const { be, stores, sys } = risaSystem(RISA_CATALOG()); // target already holds the 5
  const pick = planImport(sys(), RISA_CATALOG()) as PickState;
  const subset: PickState = { ...pick, checked: new Set([0, 2]) }; // HOU8 + DBZ

  expect(applyImport(stores, subset)).toEqual({ requested: 2, imported: 2 });
  expect(listSongs(lastSram(be)).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0", "HOU8", "DBZ"]);
});

test("applyImport preserves the target's existing songs AND live working song (end to end)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  // Target: one existing saved song ("MINE") + a DISTINCT unsaved working song a clobber would destroy.
  const working = savFrom({ workingSong: { settings: { tempo: 200 } } }).subarray(0, 0x8000).slice();
  let target = injectSong(savFrom({}), 0, "MINE", 9, savFrom({}).subarray(0, 0x8000).slice())!;
  target = target.slice();
  target.set(working, 0);
  be.setSram(id, target);

  const sys = stores.project.systems.view().find((s) => s.id === id)!;
  const pick = planImport(sys, lsdjSource()) as PickState;
  expect(applyImport(stores, { ...pick, checked: new Set([1]) }).imported).toBe(1); // import BBB only

  const out = lastSram(be);
  expect(listProjects(out).map((p) => p.name)).toEqual(["MINE", "BBB"]); // existing kept + only BBB added
  expect(sameBytes(out.subarray(0, 0x8000), working)).toBe(true); // the user's working song survived the import
});

test("applyImport reports a PARTIAL import when the target fills up (imported < requested)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  let target = savFrom({});
  for (let i = 0; i < 31; i++) target = injectSong(target, i, `S${i}`, 1, savFrom({}).subarray(0, 0x8000).slice())!; // 31/32 used
  be.setSram(id, target);

  const sys = stores.project.systems.view().find((s) => s.id === id)!;
  const pick = planImport(sys, lsdjSource()) as PickState; // 3 source songs, but only 1 free slot
  const res = applyImport(stores, { ...pick, checked: new Set([0, 1, 2]) });
  expect(res).toEqual({ requested: 3, imported: 1 }); // one landed, the caller surfaces the shortfall
  expect(listProjects(lastSram(be)).length).toBe(32); // filled to capacity, existing 31 intact + 1 imported
});

test("applyImport with nothing checked is a no-op (no write)", () => {
  const { be, stores, sys } = lsdjSystem();
  const pick = planImport(sys(), lsdjSource()) as PickState;
  const before = be.constructCalls.length;
  expect(applyImport(stores, { ...pick, checked: new Set() })).toEqual({ requested: 0, imported: 0 });
  expect(be.constructCalls.length).toBe(before); // no rebuild → nothing written
});

// --- menu wiring: the Songs "Add..." routes a picked .sav to beginSongImport --------------------------

const submenuChildren = (items: MenuItem[], id: string): MenuItem[] => {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
};
const flush = async () => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};

test("Songs Add... hands a picked .sav to beginSongImport (not the single-file importer)", async () => {
  const { be, stores, sys } = lsdjSystem();
  be.seed("/src/other.sav", lsdjSource()); // the source the browser resolves to
  be.queueBrowse("/src/other.sav");

  let imported: { id: number; bytes: number } | null = null;
  const ctx: MenuContext = {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    beginSongImport: (s, source) => (imported = { id: s.id, bytes: source.length }),
    requestExit: () => {},
    openLsdjHd: () => {},
  };

  const songs = submenuChildren(submenuChildren(buildInstanceMenu({ ...ctx, system: sys() }).items, "inst-lsdj"), "lsdj-songs");
  findItem(songs, "lsdj-song-add")!.onSelect!();
  await flush();

  expect(imported != null).toBe(true);
  expect(imported!.id).toBe(sys().id);
  expect(imported!.bytes).toBe(lsdjSource().length); // the whole .sav was handed off (not single-song import)
});
