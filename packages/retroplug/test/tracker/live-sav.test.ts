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
