// A load-time role that seeds a fresh LSDj cart's SRAM. When the store builds an LSDj system with no
// save to load, the lsdj-sync role's onConstruct hands the core a valid empty sav, so LSDj skips its
// 12–15 s cartridge self-test and boots straight to the song screen. The DECISION is proven here
// (the mock records the seeded sramBytes + the savFromJson call); byte-level codec correctness is
// proven against the native backend (test-native/app-lsdj-seed).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerRomProviders } from "../../src/romProviders";
import { gbRom, lsdjRom } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // sameboy system role
  registerDspRoles(reg); // lsdj-sync (with the seed hook) + mgb
  registerRomProviders(reg); // LSDj/mGB identity → feature roles
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("a fresh LSDj cart with no sav is seeded an empty sav at construct", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom());
  const id = store.addSystem("/roms/song.gb");
  expect(id).toBeTruthy();
  // The lsdj-sync role attached (LSDj identity), and its hook seeded sramBytes before the build.
  expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeTruthy();
  expect(be.log.includes("savFromJson")).toBeTruthy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.sramBytes != null && spec.sramBytes.byteLength > 0).toBeTruthy();
});

test("an LSDj cart with an existing .sav on disk is NOT re-seeded (never clobber a save)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom());
  be.seed("/roms/song.sav", "real battery"); // native will load this — the hook must stand down
  const id = store.addSystem("/roms/song.gb");
  expect(id).toBeTruthy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.sramBytes).toBe(undefined);
  expect(be.log.includes("savFromJson")).toBeFalsy();
});

test("a non-LSDj GB cart is never seeded (no lsdj-sync role → no hook)", () => {
  const { be, store } = newStore();
  be.seed("/roms/zelda.gb", gbRom()); // valid GB, no LSDJ title
  const id = store.addSystem("/roms/zelda.gb");
  expect(id).toBeTruthy();
  expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeFalsy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.sramBytes).toBe(undefined);
});

test("embedded mGB is never seeded (its role has no load-time hook)", () => {
  const { be, store } = newStore();
  const id = store.loadMgb();
  expect(id).toBeTruthy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.sramBytes).toBe(undefined);
});
