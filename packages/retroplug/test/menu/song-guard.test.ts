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
import { savSongName, decompressSlot, freeSongSlot, injectSong } from "../../src/lsdj/codec/sav";
import { canSaveWorkingToCatalog } from "../../src/lsdjSongOps";

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

// --- the UNLINKED LSDj path: no name to inherit, so the guard has to ask for one ---------------------

// A working song that matches no slot AND names none: LSDj keeps names on the stored project, not in the
// song, so committing this needs a name from the user.
function unlinkedDirtySav(): Uint8Array {
  const s = loadSongToWorking(twoSongSav(), 0)!;
  s[0x100] ^= 0xff; // matches no slot
  s[0x8140] = 0xff; // and names none
  return s;
}

test("an unlinked working song turns Save & Load into a name prompt", () => {
  const { songRow } = cart(unlinkedDirtySav());
  const save = findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!;
  expect(save.kind).toBe("prompt");
  expect(save.prompt!.title).toBe("Save working song as:");
  expect(save.prompt!.initial).toBe("UNTITLED");
  // LSDj song names are uppercase-only, so the field cases as you type rather than rejecting letters.
  expect(save.prompt!.casing).toBe("upper");
  expect(save.prompt!.filter!("A")).toBe(true);
  expect(save.prompt!.filter!("7")).toBe(true);
  expect(save.prompt!.filter!(" ")).toBe(true);
  expect(save.prompt!.filter!("/")).toBe(false);
});

test("the name prompt refuses an empty name and keeps itself open", () => {
  const { songRow, savOnDisk } = cart(unlinkedDirtySav());
  const save = findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!;
  // A non-null return is the error channel: the overlay stays up, shown red, and nothing was written.
  expect(save.prompt!.onConfirm("   ")).toBe("name required");
  expect(save.prompt!.onConfirm("///")).toBe("name required"); // filtered down to nothing
  expect(savOnDisk()).toBe(null);
});

test("the name prompt commits under the typed name, then loads - still one cold boot", () => {
  const { be, songRow, savOnDisk } = cart(unlinkedDirtySav());
  const edited = unlinkedDirtySav().slice(0, 0x8000);
  const writesBefore = be.constructCalls.length;
  const save = findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!;

  expect(save.prompt!.onConfirm("my song!")).toBe(null); // null closes the overlay = success

  const out = savOnDisk()!;
  // Cased + filtered + clamped to LSDj's 8 chars.
  expect(lsdjSongCatalog.list(out).some((s) => s.name === "MY SONG")).toBe(true);
  expect(lsdjSongCatalog.list(out).length).toBe(3); // a new slot, the two originals untouched
  expect([...decompressSlot(out, 2)!]).toEqual([...edited]); // the work survived
  expect(lsdjSongCatalog.workingName(out)).toBe("INTRO"); // and the picked song is loaded
  expect(be.constructCalls.length - writesBefore).toBe(1);
});

test("a full catalog offers no save, and says why instead of failing on click", () => {
  // A GENUINELY full catalog (real songs injected until no slot is free), not a hand-poked alloc table:
  // freeSongSlot has to find nothing while every listed slot still decompresses.
  let full = loadSongToWorking(twoSongSav(), 0)!;
  for (;;) {
    const slot = freeSongSlot(full);
    if (slot < 0) break;
    const next = injectSong(full, slot, `S${slot}`, 0, full.slice(0, 0x8000));
    if (!next) break; // out of block budget - full enough either way
    full = next;
  }
  // Fill FIRST, edit after: filling with the working song would make it match one of the new slots, and
  // the guard would (correctly) never appear.
  full[0x100] ^= 0xff; // matches no slot
  full[0x8140] = 0xff; // and names none - so it is the working song that has nowhere to go
  expect(canSaveWorkingToCatalog(full)).toBe(false);

  const { songRow } = cart(full);
  const children = findItem(songRow(1), "lsdj-song-1-load")!.children!;
  expect(findItem(children, "lsdj-song-1-load-save")).toBe(undefined);
  const unavailable = findItem(children, "lsdj-song-1-load-save-unavailable")!;
  expect(unavailable.label).toBe("Save Working Song & Load (no free slot)");
  expect(unavailable.disabled).toBe(true);
  // Discarding is still offered - the user is not trapped.
  expect(findItem(children, "lsdj-song-1-load-discard")?.kind).toBe("action");
});
