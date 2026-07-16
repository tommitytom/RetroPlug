// SystemsStore.addSystem — the "add instance" path: classify the ROM (TS-side),
// disambiguate a sav suffix against the LIVE list, append, auto-focus an empty
// project, and hand native a concrete resolved savPath. An unknown/absent ROM is
// rejected before any native construct.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom, garbage } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  let changes = 0;
  const store = new SystemsStore(be, () => changes++);
  return { be, store, changes: () => changes };
}

test("add: classifies, appends, and auto-focuses the first system", () => {
  const { be, store, changes } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb");
  expect(id).toBeTruthy();
  const v = store.view();
  expect(v.length).toBe(1);
  expect(v[0].platform).toBe("gb");
  expect(v[0].romPath).toBe("/roms/a.gb");
  expect(v[0].focused).toBeTruthy(); // first system into an empty project
  expect(v[0].missing).toBeFalsy(); // the ROM is on the fake disk
  expect(store.focused()).toBe(id);
  expect(store.isDirty()).toBeTruthy();
  expect(changes()).toBe(1);
});

test("add: passes native a concrete resolved savPath (the suffix-0 sibling)", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  store.addSystem("/roms/a.gb");
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romPath).toBe("/roms/a.gb");
  expect(spec.embeddedRom).toBe("");
  expect(spec.savPath).toBe("/roms/a.gb".replace(".gb", ".sav")); // /roms/a.sav
  expect(spec.statePath).toBe(null);
});

test("add: disambiguates the sav suffix against the live list (0, then 2, then 3)", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  store.addSystem("/roms/a.gb"); // suffix 0
  store.addSystem("/roms/a.gb"); // 0 owned -> 2
  store.addSystem("/roms/a.gb"); // 0,2 owned -> 3
  expect(store.view().map((v) => v.savSuffix)).toEqual([0, 2, 3]);
  // concrete savPaths the store handed native
  expect(be.constructCalls.map((c) => c.savPath)).toEqual([
    "/roms/a.sav",
    "/roms/a-2.sav",
    "/roms/a-3.sav",
  ]);
});

test("add: grows past a suffix whose <rom>-N.sav already exists on disk", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a-2.sav", "orphan battery"); // a since-removed instance's file
  store.addSystem("/roms/a.gb"); // suffix 0
  store.addSystem("/roms/a.gb"); // 0 owned, 2 on disk -> 3
  expect(store.view().map((v) => v.savSuffix)).toEqual([0, 3]);
});

test("add: an unknown or absent ROM is a no-op (rejected before construct)", () => {
  const { be, store, changes } = newStore();
  be.seed("/roms/notarom.gb", garbage()); // present but classifies unknown
  expect(store.addSystem("/roms/notarom.gb")).toBe(null);
  expect(store.addSystem("/roms/missing.gb")).toBe(null); // not on disk at all
  expect(store.view().length).toBe(0);
  expect(changes()).toBe(0);
  expect(be.log.includes("constructSystem")).toBeFalsy(); // never reached native
});

test("focusNext: cycles focus forward/back through the instances in grid order, wrapping", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const a = store.addSystem("/roms/a.gb") as number; // focused (first)
  const b = store.addSystem("/roms/a.gb") as number;
  const c = store.addSystem("/roms/a.gb") as number; // order: [a, b, c], focus still a

  expect(store.focused()).toBe(a);
  expect(store.focusNext(1)).toBeTruthy(); // a → b
  expect(store.focused()).toBe(b);
  expect(store.focusNext(1)).toBeTruthy(); // b → c
  expect(store.focusNext(1)).toBeTruthy(); // c → a (wrap)
  expect(store.focused()).toBe(a);
  expect(store.focusNext(-1)).toBeTruthy(); // a → c (wrap back)
  expect(store.focused()).toBe(c);
});

test("focusNext: a no-op with fewer than two instances", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  expect(store.focusNext(1)).toBeFalsy(); // empty
  store.addSystem("/roms/a.gb");
  expect(store.focusNext(1)).toBeFalsy(); // single instance — nowhere to go
});
