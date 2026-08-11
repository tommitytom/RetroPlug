// The LSDj song menu's Move Up/Down (reorder). LSDj addresses saved songs by a FIXED slot number, so reorder
// swaps two saved slots — and the shared songMenu passes LIST POSITIONS (not the row's slot index) so it works
// for LSDj's SPARSE slots. This guards that the generic menu drives the LSDj catalog's reorder end-to-end
// (readSram → swap → loadSram), the LSDj half of the song-menu unification.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { savFrom, injectSong, listProjects, loadSongToWorking, decompressSlot, decodeLsdsngRaw } from "../../src/lsdjSav";
import { ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { gbRomBattery } from "../systems/fixtures";

function ctxOf(stores: AppStores): MenuContext {
  return {
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
    beginSongImport: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
  };
}
const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// A full 1 MiB LSDj ROM (title "LSDJ…" → the provider attaches lsdj-sync, and isLsdj → the submenu appears).
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

// A battery with two songs at SPARSE slots 0 and 3 (the case that distinguishes list position from slot).
function twoSongSav(): Uint8Array {
  const song = savFrom({ workingSong: { settings: { tempo: 120 } } }).subarray(0, 0x8000).slice();
  let sav = savFrom({});
  sav = injectSong(sav, 0, "AAA", 1, song)!;
  sav = injectSong(sav, 3, "BBB", 2, song)!;
  return sav;
}

test("the LSDj song menu offers Move Up/Down (disabled at the ends) and swaps sparse slots end-to-end", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  be.setSram(id, twoSongSav());
  const songs = () =>
    submenuChildren(submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj"), "lsdj-songs");

  // Rows are the occupied slots sorted by slot: [0] AAA, [3] BBB. Both offer Move Up/Down.
  expect(songs().filter((s) => s.kind === "submenu").map((s) => s.label)).toEqual(["[0] AAA", "[3] BBB"]);
  const row0 = submenuChildren(songs(), "lsdj-song-0");
  expect(findItem(row0, "lsdj-song-0-up")?.disabled).toBe(true); // first in the list → can't move up
  expect(findItem(row0, "lsdj-song-0-down")?.disabled).toBeFalsy();
  expect(findItem(submenuChildren(songs(), "lsdj-song-3"), "lsdj-song-3-down")?.disabled).toBe(true); // last → can't move down

  // Move Down on the first row (position 0) → swaps slots 0 and 3 → [0] BBB, [3] AAA.
  findItem(submenuChildren(songs(), "lsdj-song-0"), "lsdj-song-0-down")!.onSelect!();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  expect(listProjects(spec.sramBytes!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:BBB", "3:AAA"]);
});

// The synthetic [working] row, the LSDj half of the shared working-song handling. It shows on the same
// content gate risa uses (the working song holds work no saved slot has), which for LSDj means the
// linked-and-edited case: loaded a song, worked on it, not yet committed back to its slot.
test("a LINKED, edited LSDj working song gets the [working] row, offering Save Changes + Export", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  const edited = loadSongToWorking(twoSongSav(), 0)!; // working memory is now slot 0 (AAA)...
  edited[0x100] ^= 0xff; // ...and has been edited since
  be.setSram(id, edited);
  const songs = () =>
    submenuChildren(submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj"), "lsdj-songs");

  // It borrows slot 0's name (LSDj keeps names on the stored project, not in the song).
  expect(songs().find((s) => s.id === "lsdj-song-working")?.label).toBe("[working] AAA (unsaved)");
  const w = submenuChildren(songs(), "lsdj-song-working");
  // Linked, so saving overwrites slot 0 rather than claiming a new one - and no same-name overwrite rows,
  // which exist only for a working song that names no slot.
  expect(findItem(w, "lsdj-song-working-save")?.label).toBe("Save Changes");
  expect(findItem(w, "lsdj-song-working-save-0")).toBe(undefined);
  expect(findItem(w, "lsdj-song-working-export")?.kind).toBe("action");

  // Save Changes writes the edit into slot 0 and leaves the song count alone.
  findItem(w, "lsdj-song-working-save")!.onSelect!();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  expect(listProjects(spec.sramBytes!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:AAA", "3:BBB"]);
  expect(decompressSlot(spec.sramBytes!, 0)![0x100]).toBe(edited[0x100]); // the edit really landed
});

test("a CLEAN LSDj working song shows no row, and an UNLINKED one shows none either (it has no name)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  const songs = () =>
    submenuChildren(submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj"), "lsdj-songs");

  // Freshly loaded: working memory IS slot 0, so there is nothing to save and no row.
  be.setSram(id, loadSongToWorking(twoSongSav(), 0)!);
  expect(songs().find((s) => s.id === "lsdj-song-working")).toBe(undefined);

  // Unlinked: still dirty (so Load still warns), but LSDj has no name anywhere to label a row with, so it
  // stays row-less. The load guard is where that case is handled, because it can prompt for a name.
  const unlinked = loadSongToWorking(twoSongSav(), 0)!;
  unlinked[0x100] ^= 0xff;
  unlinked[0x8140] = 0xff;
  be.setSram(id, unlinked);
  expect(songs().find((s) => s.id === "lsdj-song-working")).toBe(undefined);
  expect(findItem(submenuChildren(songs(), "lsdj-song-3"), "lsdj-song-3-load")?.kind).toBe("submenu"); // guarded
});

test("Export... on the LSDj working row writes the LIVE working song (edits included) as a .lsdsng", async () => {
  // A leaf that browses fires openFileBrowser fire-and-forget; flush the microtask chain it kicks off.
  const flush = async () => {
    for (let i = 0; i < 8; i++) await Promise.resolve();
  };
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  const edited = loadSongToWorking(twoSongSav(), 0)!;
  edited[0x100] ^= 0xff;
  be.setSram(id, edited);
  be.queueBrowse("/out/working.lsdsng");

  const w = submenuChildren(
    submenuChildren(submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj"), "lsdj-songs"),
    "lsdj-song-working",
  );
  findItem(w, "lsdj-song-working-export")!.onSelect!();
  await flush();

  const written = decodeLsdsngRaw(be.readFile("/out/working.lsdsng")!);
  expect(written.name).toBe("AAA"); // borrowed from the linked slot, like the row's label
  // The UNCOMMITTED edit is in the file - the point of exporting the working song rather than its slot,
  // which still holds the pre-edit byte.
  expect(written.songBytes[0x100]).toBe(edited[0x100]);
  expect(written.songBytes[0x100] === decompressSlot(edited, 0)![0x100]).toBe(false);
});
