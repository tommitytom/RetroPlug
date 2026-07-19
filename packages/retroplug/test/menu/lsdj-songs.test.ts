// The LSDj song menu's Move Up/Down (reorder). LSDj addresses saved songs by a FIXED slot number, so reorder
// swaps two saved slots — and the shared songMenu passes LIST POSITIONS (not the row's slot index) so it works
// for LSDj's SPARSE slots. This guards that the generic menu drives the LSDj catalog's reorder end-to-end
// (readSram → swap → loadSram), the LSDj half of the song-menu unification.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { savFrom, injectSong, listProjects } from "../../src/lsdjSav";
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
