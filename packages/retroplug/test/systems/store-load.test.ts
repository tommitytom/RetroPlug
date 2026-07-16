// SystemsStore.loadRom / loadMgb / replaceSystem — the "load" family: replace the
// focused tile in place (adopt into an empty project), defer to a sibling <rom>.rplg
// when one exists (unless a save was paired), and build the embedded mGB.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be);
  return { be, store };
}

test("load: into an empty store adopts + focuses", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const r = store.loadRom("/roms/a.gb");
  expect(r).toEqual({ system: store.focused() });
  expect(store.view().length).toBe(1);
  expect(store.view()[0].focused).toBeTruthy();
});

test("load: replaces the focused system in place (new id, same slot, focus follows)", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const first = store.addSystem("/roms/a.gb");
  const r = store.loadRom("/roms/b.gb"); // replaces the focused (only) system
  expect(store.view().length).toBe(1); // replaced, not appended
  expect(store.view()[0].romPath).toBe("/roms/b.gb");
  const newId = (r as { system: number }).system;
  expect(newId === first).toBeFalsy(); // a replaced system gets a fresh id
  expect(store.focused()).toBe(newId);
});

test("load: defers to a sibling <rom>.rplg and builds nothing", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.rplg", "project bytes");
  const r = store.loadRom("/roms/a.gb");
  expect(r).toEqual({ deferredProject: "/roms/a.rplg" });
  expect(store.view().length).toBe(0);
  expect(be.log.includes("constructSystem")).toBeFalsy();
});

test("load: a paired save does NOT defer, and pins the different file as the savPath", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.rplg", "project bytes"); // present, but the paired save wins
  const r = store.loadRom("/roms/a.gb", { explicitSav: "/saves/mine.sav" });
  expect("system" in (r as object)).toBeTruthy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.savPath).toBe("/saves/mine.sav"); // a genuinely different file -> override
});

test("load: a paired save equal to the natural sibling is NOT an override", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  store.loadRom("/roms/a.gb", { explicitSav: "/roms/a.sav" }); // == the suffix-0 sibling
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.savPath).toBe("/roms/a.sav"); // resolves to the sibling either way, no override pinned
  expect(store.view()[0].savSuffix).toBe(0);
});

test("loadMgb: builds the embedded ROM (sameboy, no file, no header read)", () => {
  const { be, store } = newStore();
  const id = store.loadMgb();
  expect(id).toBeTruthy();
  const v = store.view();
  expect(v[0].platform).toBe("gb");
  expect(v[0].core).toBe("sameboy");
  expect(v[0].embedded).toBeTruthy();
  expect(v[0].romPath).toBe("");
  expect(v[0].missing).toBeFalsy(); // embedded is never missing
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.embeddedRom).toBe("mgb");
  expect(spec.romPath).toBe("");
  expect(spec.savPath).toBe(null);
  expect(be.log.includes("readFilePrefix")).toBeFalsy(); // embedded skips classification
});

test("replaceSystem: swaps a specific id in place and re-points focus if it was focused", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.gb", gbRom());
  const a = store.addSystem("/roms/a.gb"); // focused
  const b = store.addSystem("/roms/b.gb"); // not focused
  const newId = store.replaceSystem(b as number, "/roms/c.gb");
  const v = store.view();
  expect(v.map((s) => s.romPath)).toEqual(["/roms/a.gb", "/roms/c.gb"]); // b's slot swapped
  expect(store.focused()).toBe(a); // a stayed focused (b wasn't)
  expect(newId === b).toBeFalsy();
});
