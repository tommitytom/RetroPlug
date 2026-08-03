// Editing a running cart's battery: mutateLiveSav (the shape every Songs-menu edit takes) and
// loadSongByName (what a recent song row runs once its project has loaded). Driven over the REAL
// SystemsStore + role registry against the MockBackend, so the role-derived song catalog, the
// resolved .sav target and the cold-boot rebuild are all exercised, not stubbed.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { buildAppRegistry } from "../../src/appHost";
import { mutateLiveSav, loadSongByName, loadSongInPrimary, lsdjSongCatalog } from "../../src/tracker";
import { lsdjRom, gbRomBattery } from "../systems/fixtures";
import { savFrom, type SavInput } from "../../src/lsdjSav";

const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };

// An LSDj battery with GRUB + INTRO saved and `active` loaded into working memory.
function lsdjSav(active: number): Uint8Array {
  return savFrom({
    activeProjectIndex: active,
    projects: [
      { name: "GRUB", version: 0, song: SONG },
      { name: "INTRO", version: 0, song: SONG },
    ],
  } as SavInput);
}

// A live LSDj system with GRUB working. The store carries the role registry, so the cart resolves a song
// catalog exactly as it does in the app.
function newCart(rom: Uint8Array = lsdjRom("LSDJ-V9.4.2")) {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/lsdj.gb", rom);
  const id = systems.addSystem("/roms/lsdj.gb")!;
  be.setSram(id, lsdjSav(0));
  return { be, systems, sys: () => systems.systems()[0] };
}

test("mutateLiveSav: writes the resolved .sav and cold-boots the cart from it", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO"); // durable on disk
  expect(sys().id).toBe(before + 1); // rebuilt in place (a new core id), so the running cart followed
});

test("mutateLiveSav: a declining transform leaves the cart and its .sav untouched", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;
  expect(mutateLiveSav(be, systems, sys(), () => null)).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav")).toBe(null); // nothing written
  expect(sys().id).toBe(before); // nothing rebuilt
});

test("loadSongByName: loads that song into working memory, addressing it by NAME not slot", () => {
  const { be, systems, sys } = newCart();
  expect(loadSongByName(be, systems, sys(), "INTRO")).toBeTruthy();
  // The cart is booted from the written battery, so that file IS the loaded state (the mock backend has no
  // core to read a fresh snapshot back from).
  const written = be.readFile("/roms/lsdj.sav")!;
  expect(lsdjSongCatalog.workingName(written)).toBe("INTRO");
  expect(lsdjSongCatalog.list(written).map((s) => s.name)).toEqual(["GRUB", "INTRO"]); // both songs still saved
});

test("loadSongByName: the song already loaded is a no-op (no rebuild, no write)", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;
  expect(loadSongByName(be, systems, sys(), "GRUB")).toBeFalsy(); // GRUB is already working
  expect(sys().id).toBe(before);
  expect(be.readFile("/roms/lsdj.sav")).toBe(null);
});

test("loadSongByName: an unknown song / a non-tracker cart declines instead of touching the battery", () => {
  const { be, systems, sys } = newCart();
  expect(loadSongByName(be, systems, sys(), "GONE")).toBeFalsy(); // deleted or renamed since it was recorded
  expect(loadSongByName(be, systems, sys(), "")).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav")).toBe(null);

  const plain = newCart(gbRomBattery()); // a battery cart with no song catalog
  expect(loadSongByName(plain.be, plain.systems, plain.sys(), "GRUB")).toBeFalsy();
});

test("loadSongInPrimary: targets the focused system, and declines with no systems at all", () => {
  const { be, systems } = newCart();
  const second = systems.addSystem("/roms/lsdj.gb")!; // a second instance, taking /roms/lsdj-2.sav
  be.setSram(second, lsdjSav(0));
  systems.setFocus(second);

  expect(loadSongInPrimary(be, systems, "INTRO")).toBeTruthy();
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj-2.sav")!)).toBe("INTRO"); // the FOCUSED one
  expect(be.readFile("/roms/lsdj.sav")).toBe(null); // the unfocused instance was left alone

  const empty = new SystemsStore(new MockBackend("/cfg"), () => {}, buildAppRegistry());
  expect(loadSongInPrimary(be, empty, "INTRO")).toBeFalsy();
});

// --- the rolling backup -----------------------------------------------------------------------------
// Every destructive battery edit goes through mutateLiveSav, so backing up there covers Load / Replace /
// Delete / Add / reorder at once - including any op added later that forgets to think about it. It is the
// last line of defence when a confirm is dismissed, or a path grows that never raises one.

test("mutateLiveSav: writes a <sav>.bak of the PRE-EDIT battery before overwriting the .sav", () => {
  const { be, systems, sys } = newCart();
  const before = be.readSram(sys().id)!; // GRUB working

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  // The .sav is the NEW state...
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO");
  // ...and the backup is exactly what was there before, so the discarded working song is recoverable.
  const bak = be.readFile("/roms/lsdj.sav.bak");
  expect(bak != null).toBeTruthy();
  expect([...bak!]).toEqual([...before]);
  expect(lsdjSongCatalog.workingName(bak!)).toBe("GRUB");
});

test("mutateLiveSav: the backup is the LIVE battery, not the stale copy on disk", () => {
  const { be, systems, sys } = newCart();
  // Simulate the OnProjectSave default: an older mirror on disk while the live battery has moved on.
  be.writeFile("/roms/lsdj.sav", lsdjSav(1));
  const live = be.readSram(sys().id)!; // still GRUB working

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  // Backing up the file would have preserved the stale INTRO state and lost the live one.
  expect([...be.readFile("/roms/lsdj.sav.bak")!]).toEqual([...live]);
});

test("mutateLiveSav: a declining transform writes no backup either", () => {
  const { be, systems, sys } = newCart();
  expect(mutateLiveSav(be, systems, sys(), () => null)).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav.bak")).toBe(null);
});
