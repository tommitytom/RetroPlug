// The Songs menu's data-loss guards, at the menu level: what the rows actually become when the cart holds
// uncommitted work, and that each way out does what it says. The predicate behind the decision is proven in
// test/tracker/workingSongDirty; this is about the UI it drives.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { lsdjRom } from "../systems/fixtures";
import { savFrom, loadSongToWorking, type SavInput } from "../../src/lsdjSav";
import { lsdjSongCatalog } from "../../src/tracker";
import { savSongName, decompressSlot } from "../../src/lsdj/codec/sav";

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);
const submenuChildren = (items: MenuItem[], id: string): MenuItem[] => findItem(items, id)?.children ?? [];

const song = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
const twoSongSav = () =>
  savFrom({ activeProjectIndex: 0, projects: [{ name: "GRUB", version: 0, song }, { name: "INTRO", version: 0, song }] } as SavInput);

// A cart with `sram` loaded, and a fresh accessor for its LSDj song rows.
function cart(sram: Uint8Array) {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/cool.gb", lsdjRom("LSDJ-V9.4.2"));
  stores.project.systems.loadRom("/roms/cool.gb");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, sram);
  const ctx = (): MenuContext => ({
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
    requestExit: () => {},
    beginSongImport: () => {},
  });
  const songRow = (index: number): MenuItem[] => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    const inst = buildInstanceMenu({ ...ctx(), system: sys }).items;
    return submenuChildren(submenuChildren(submenuChildren(inst, "inst-lsdj"), "lsdj-songs"), `lsdj-song-${index}`);
  };
  return { be, stores, id, songRow, savOnDisk: () => be.readFile("/roms/cool.sav") };
}

test("a clean working song leaves Load a plain action - no prompt when nothing would be lost", () => {
  const { songRow } = cart(loadSongToWorking(twoSongSav(), 0)!);
  const load = findItem(songRow(1), "lsdj-song-1-load")!;
  expect(load.kind).toBe("action");
  expect(typeof load.onSelect).toBe("function");
});

test("uncommitted work turns Load into a guard naming the song at risk", () => {
  const dirty = loadSongToWorking(twoSongSav(), 0)!;
  dirty[0x100] ^= 0xff; // edits to GRUB that live in no slot
  const { songRow } = cart(dirty);

  const load = findItem(songRow(1), "lsdj-song-1-load")!;
  expect(load.kind).toBe("submenu");
  // Names the casualty, and that row is inert (greyed, skipped by nav) - it is a statement, not an option.
  const warn = findItem(load.children!, "lsdj-song-1-load-warn")!;
  expect(warn.label).toBe('"GRUB" has unsaved changes');
  expect(warn.disabled).toBe(true);
  // Both ways forward are offered; backing out of the submenu is the cancel.
  expect(findItem(load.children!, "lsdj-song-1-load-save")?.kind).toBe("action"); // GRUB is linked → no name needed
  expect(findItem(load.children!, "lsdj-song-1-load-discard")?.kind).toBe("action");
});

test("Discard & Load throws the working song away and loads the picked one", () => {
  const dirty = loadSongToWorking(twoSongSav(), 0)!;
  dirty[0x100] ^= 0xff;
  const { songRow, savOnDisk } = cart(dirty);

  findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-discard")!.onSelect!();

  const out = savOnDisk()!;
  expect(lsdjSongCatalog.workingName(out)).toBe("INTRO"); // the picked song is loaded
  expect(lsdjSongCatalog.workingSongDirty!(out)).toBe(false); // and it is a clean copy of its slot
});

test("Save & Load commits the working song to its own slot FIRST, then loads - in one write", () => {
  const dirty = loadSongToWorking(twoSongSav(), 0)!;
  dirty[0x100] ^= 0xff;
  const edited = dirty.slice(0, 0x8000);
  const { be, songRow, savOnDisk } = cart(dirty);
  const writesBefore = be.constructCalls.length;

  findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!.onSelect!();

  const out = savOnDisk()!;
  // The edit survived, in GRUB's own slot (not a duplicate)...
  expect(lsdjSongCatalog.list(out).length).toBe(2);
  expect(savSongName(out, 0)).toBe("GRUB");
  expect([...decompressSlot(out, 0)!]).toEqual([...edited]);
  // ...and INTRO is now loaded.
  expect(lsdjSongCatalog.workingName(out)).toBe("INTRO");
  // ONE cold boot, not two: save+load is a single byte-op, so the second half can't run against a stale
  // system id (loadSram allocates a new one every rebuild).
  expect(be.constructCalls.length - writesBefore).toBe(1);
});

test("Replace confirms before touching a saved slot", () => {
  const { songRow } = cart(loadSongToWorking(twoSongSav(), 0)!);
  const replace = findItem(songRow(1), "lsdj-song-1-replace")!;
  expect(replace.kind).toBe("prompt");
  expect(replace.prompt!.confirm).toBe(true);
  expect(replace.prompt!.title).toBe('Replace saved song "INTRO"?');
});
