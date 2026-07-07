// SystemsStore duplicate / remove / reload + the sav→ROM pairing helper. Duplicate
// clones live state with a fresh suffix; remove splices + refocuses; reload swaps in
// place preserving identity with a new id; resolveSiblingRom pairs a picked .sav.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom, gbaRom, garbage } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be);
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

test("resolveSiblingRom: picks the sibling ROM, skipping a present non-ROM of the same stem", () => {
  const { be, store } = newStore();
  be.seed("/roms/game.gb", garbage()); // present but classifies unknown -> skipped
  be.seed("/roms/game.gba", gbaRom()); // present + valid -> the pick
  expect(store.resolveSiblingRom("/roms/game.sav")).toBe("/roms/game.gba");
  expect(store.resolveSiblingRom("/roms/none.sav")).toBe(null);
});
