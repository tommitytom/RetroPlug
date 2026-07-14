// SystemsStore duplicate / remove / reload + save-load state/sram + the sav→ROM pairing helper.
// Duplicate clones live state with a fresh suffix; remove splices + refocuses; reload + loadState/
// loadSram swap in place preserving identity with a new id; saveState/saveSram dump the registry read
// to disk; resolveSiblingRom pairs a picked .sav.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor, sramBytesFor } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { buildAppRegistry } from "../../src/appHost";
import { gbRom, gbaRom, nesRom, lsdjRom, garbage } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be);
  return { be, store };
}

// A store with the real role registry, so constructed systems carry their `sameboy` role (and its
// linkGroupId) — needed for the link-group tests below (the bare store above builds roleless systems).
function newStoreWithRoles() {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be, () => {}, buildAppRegistry());
  return { be, store };
}

test("duplicate: appends a clone with a fresh suffix + concrete auto-save path", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb"); // suffix 0, focused
  const dup = store.duplicateSystem(a as number);
  expect(dup).toBeTruthy();
  const v = store.view();
  expect(v.length).toBe(2);
  expect(v[1].platform).toBe("gb");
  expect(v[1].romPath).toBe("/roms/a.gb"); // clone carries the source ROM
  expect(v[1].savSuffix).toBe(2); // 0 owned -> 2
  expect(store.focused()).toBe(a); // duplicate doesn't steal focus
  // Duplicate is TS orchestration now: readState(src) → constructSystem seeded with those bytes + the
  // clone's own auto-save path. No bespoke backend method — it lands as a construct call.
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.romPath).toBe("/roms/a.gb");
  expect(call.savPath).toBe("/roms/a-2.sav");
  expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(a as number));
});

test("duplicate: an absent id is a no-op", () => {
  const { store } = newStore();
  expect(store.duplicateSystem(999)).toBe(null);
  expect(store.view().length).toBe(0);
});

test("remove: splices out + refocuses the front when the focused system went away", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const a = store.addSystem("/roms/a.gb"); // focused
  const b = store.addSystem("/roms/b.gb");
  expect(store.removeSystem(a as number)).toBeTruthy();
  expect(store.view().map((s) => s.id)).toEqual([b]);
  expect(store.focused()).toBe(b); // focus fell to the new front
});

test("remove: an absent id is false; removing the last clears focus", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb");
  expect(store.removeSystem(4242)).toBeFalsy();
  expect(store.removeSystem(a as number)).toBeTruthy();
  expect(store.view().length).toBe(0);
  expect(store.focused()).toBe(0);
});

test("reload: swaps in place preserving identity, with a new id + focus", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb");
  const newId = store.reloadSystem(a as number);
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  const v = store.view();
  expect(v.length).toBe(1); // swapped, not appended
  expect(v[0].id).toBe(newId);
  expect(v[0].romPath).toBe("/roms/a.gb"); // identity preserved
  expect(v[0].savSuffix).toBe(0);
  expect(store.focused()).toBe(newId); // focus followed the swap
  expect(store.reloadSystem(9999)).toBe(null); // absent -> no-op
});

test("swapRom: changes the ROM in place carrying the live battery, reclassifying the new ROM", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom()); // a different platform, to prove the new ROM is classified afresh
  const a = store.addSystem("/roms/a.gb") as number;
  const newId = store.swapRom(a, "/roms/b.nes");
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  const v = store.view();
  expect(v.length).toBe(1); // swapped, not appended
  expect(v[0].id).toBe(newId);
  expect(v[0].romPath).toBe("/roms/b.nes"); // the new ROM
  expect(v[0].platform).toBe("nes"); // reclassified — platform/core follow the new cart
  expect(v[0].savSuffix).toBe(0);
  expect(store.focused()).toBe(newId); // focus followed the swap
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(a); // in-place replace
  expect(call.romPath).toBe("/roms/b.nes");
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(a)); // the OLD system's live battery carried forward
  expect(call.stateBytes).toBe(undefined); // cold boot with the carried SRAM, no savestate
  expect(store.swapRom(9999, "/roms/b.nes")).toBe(null); // absent id -> no-op
  expect(store.swapRom(newId as number, "/roms/none.gb")).toBe(null); // unclassifiable ROM -> no-op, untouched
  expect(store.view()[0].romPath).toBe("/roms/b.nes"); // the failed swap left the instance in place
});

test("swapRom: swapping a background (non-focused) instance leaves focus put", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.nes", nesRom());
  const a = store.addSystem("/roms/a.gb") as number;
  const b = store.addSystem("/roms/b.gb") as number;
  expect(store.setFocus(b)).toBeTruthy();
  const newA = store.swapRom(a, "/roms/c.nes");
  expect(newA).toBeTruthy();
  expect(store.focused()).toBe(b); // focus stayed on the untouched instance (the guard's false branch)
  const v = store.view();
  expect(v.find((s) => s.id === b)!.romPath).toBe("/roms/b.gb"); // b untouched
  expect(v.find((s) => s.id === newA)!.romPath).toBe("/roms/c.nes"); // a was the one swapped
});

test("swapRom: an LSDj→LSDj swap carries the song forward — the seed-hook stands down for the live SRAM", () => {
  const { be, store } = newStoreWithRoles(); // registry attaches the lsdj-sync role + its onConstruct seed hook
  be.seed("/roms/v1.gb", lsdjRom());
  be.seed("/roms/v2.gb", lsdjRom("LSDJV2")); // a fresh LSDj cart, no on-disk .sav
  const a = store.addSystem("/roms/v1.gb") as number;
  // The fresh v1 add seeds its own empty sav (savFromJson); count that so we can prove the SWAP adds none.
  const seedsBeforeSwap = be.log.filter((e) => e === "savFromJson").length;
  const newId = store.swapRom(a, "/roms/v2.gb");
  expect(newId).toBeTruthy();
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.romPath).toBe("/roms/v2.gb");
  // The carried live battery must survive: lsdjSeedSav sees spec.sramBytes set and stands down, so it does
  // NOT overwrite it with an empty savFromJson("{}") seed. This is the whole point of the feature.
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(a));
  expect(be.log.filter((e) => e === "savFromJson").length).toBe(seedsBeforeSwap); // no empty-sav seed during the swap
  expect(store.view()[0].roles.some((r) => r.kind === "lsdj-sync")).toBeTruthy(); // new cart re-sniffed as LSDj
});

test("saveState: dumps the system's published savestate to the picked path", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb") as number;
  expect(store.saveState(a, "/out/a.ss0")).toBeTruthy();
  expect(be.readFile("/out/a.ss0")).toEqual(stateBytesFor(a)); // the registry read reached disk
  expect(store.saveState(9999, "/out/x.ss0")).toBeFalsy(); // absent -> false, nothing written
});

test("saveSram: dumps the system's battery SRAM to the picked path", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb") as number;
  expect(store.saveSram(a, "/out/a.sav")).toBeTruthy();
  expect(be.readFile("/out/a.sav")).toEqual(sramBytesFor(a));
});

test("loadState: reconstructs the system in place, booted from the file's bytes", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/states/x.ss0", stateBytesFor(999)); // arbitrary savestate bytes
  const a = store.addSystem("/roms/a.gb") as number;
  const newId = store.loadState(a, "/states/x.ss0");
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  const v = store.view();
  expect(v.length).toBe(1); // swapped, not appended
  expect(v[0].id).toBe(newId);
  expect(store.focused()).toBe(newId); // focus followed the swap
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(a); // in-place replace
  expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(999)); // the file's bytes seeded the core
  expect(store.loadState(9999, "/states/x.ss0")).toBe(null); // absent id -> no-op
  expect(store.loadState(newId as number, "/nope.ss0")).toBe(null); // unreadable file -> no-op
});

test("loadSram: cold-boots the ROM in place with the file's SRAM", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/saves/x.sav", sramBytesFor(999));
  const a = store.addSystem("/roms/a.gb") as number;
  const newId = store.loadSram(a, "/saves/x.sav");
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(a);
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(999));
});

test("reset: reboots in place carrying the live battery, with a new id + focus", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb") as number;
  const newId = store.reset(a);
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  const v = store.view();
  expect(v.length).toBe(1); // swapped, not appended
  expect(v[0].id).toBe(newId);
  expect(v[0].romPath).toBe("/roms/a.gb"); // identity preserved
  expect(store.focused()).toBe(newId); // focus followed the swap
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(a); // in-place replace
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(a)); // the live battery carried forward
  expect(call.stateBytes).toBe(undefined); // cold boot, no savestate
  expect(store.reset(9999)).toBe(null); // absent -> no-op
});

test("newSram: cold-boots in place with a blank (all-zero) battery", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb") as number;
  const newId = store.newSram(a);
  expect(newId).toBeTruthy();
  expect(newId === a).toBeFalsy();
  expect(store.view().length).toBe(1); // swapped, not appended
  expect(store.focused()).toBe(newId); // focus followed the swap
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(a);
  const seed = new Uint8Array(call.sramBytes!);
  expect(seed.length).toBe(0x20000); // native truncates/zero-pads this to the cart's real battery size
  expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
  expect(call.stateBytes).toBe(undefined); // cold boot, no savestate
  expect(store.newSram(9999)).toBe(null); // absent -> no-op
});

test("resolveSiblingRom: picks the sibling ROM, skipping a present non-ROM of the same stem", () => {
  const { be, store } = newStore();
  be.seed("/roms/game.gb", garbage()); // present but classifies unknown -> skipped
  be.seed("/roms/game.gba", gbaRom()); // present + valid -> the pick
  expect(store.resolveSiblingRom("/roms/game.sav")).toBe("/roms/game.gba");
  expect(store.resolveSiblingRom("/roms/none.sav")).toBe(null);
});

const linkGroupOf = (store: SystemsStore, id: number): number =>
  (store.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "sameboy")!.config as { linkGroupId: number }).linkGroupId;

test("inheritLinkGroup: child joins the parent's group; a lone parent (0) is promoted to 1", () => {
  const { be, store } = newStoreWithRoles();
  be.seed("/roms/a.gb", gbRom());
  const parent = store.addSystem("/roms/a.gb") as number; // group 0
  const child = store.addSystem("/roms/a.gb") as number; // group 0

  store.inheritLinkGroup(child, parent); // parent ungrouped → both promoted to 1
  expect(linkGroupOf(store, parent)).toBe(1);
  expect(linkGroupOf(store, child)).toBe(1);

  // Parent already in a group → the child matches it and the parent is left untouched.
  const child2 = store.addSystem("/roms/a.gb") as number;
  store.setRoleConfig(parent, "sameboy", { linkGroupId: 3 });
  store.inheritLinkGroup(child2, parent);
  expect(linkGroupOf(store, parent)).toBe(3);
  expect(linkGroupOf(store, child2)).toBe(3);
});

test("inheritLinkGroup: a non-SameBoy parent is a no-op (link groups are a GB concept)", () => {
  const { be, store } = newStoreWithRoles();
  be.seed("/roms/a.gba", gbaRom()); // mesen core — no sameboy role
  be.seed("/roms/b.gb", gbRom());
  const parent = store.addSystem("/roms/a.gba") as number;
  const child = store.addSystem("/roms/b.gb") as number;
  store.inheritLinkGroup(child, parent);
  expect(linkGroupOf(store, child)).toBe(0); // nothing promoted or assigned
});
