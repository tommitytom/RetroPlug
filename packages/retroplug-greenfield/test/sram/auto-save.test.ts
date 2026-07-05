// The SRAM auto-save (mirror) write policy: the pure dedup/seed/write decision, and the
// SramAutoSaver over a MockBackend + real SystemsStore + UserConfigStore. Proves the
// mode gating (Off/OnProjectSave/Continuous), dirty-hash dedup, seed-vs-write on first
// observation, the override write target, and skipping embedded systems.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { UserConfigStore } from "../../src/userConfigStore";
import { SramAutoSaver, hashBytes, decideAutoSave } from "../../src/sramAutoSave";
import { gbRom } from "../systems/fixtures";

const bytes = (...b: number[]) => new Uint8Array(b);
const SAV = "/proj/a.sav";

// --- pure kernel ---

test("hashBytes: stable and content-sensitive", () => {
  expect(hashBytes(bytes(1, 2, 3))).toBe(hashBytes(bytes(1, 2, 3)));
  expect(hashBytes(bytes(1, 2, 3)) === hashBytes(bytes(1, 2, 4))).toBeFalsy();
});

test("decideAutoSave: dedup / seed / write", () => {
  const sav = bytes(1, 2, 3);
  const h = hashBytes(sav);
  expect(decideAutoSave(sav, h, null)).toEqual({ write: false, hash: h }); // unchanged
  expect(decideAutoSave(sav, null, bytes(1, 2, 3))).toEqual({ write: false, hash: h }); // first-obs, matching file → seed
  expect(decideAutoSave(sav, null, null)).toEqual({ write: true, hash: h }); // first-obs, no file → write
  expect(decideAutoSave(sav, null, bytes(9)).write).toBeTruthy(); // first-obs, different file → write
  expect(decideAutoSave(sav, h + 1, null).write).toBeTruthy(); // changed → write
});

// --- store ---

function setup() {
  const be = new MockBackend("/config");
  const uc = new UserConfigStore(be); // default sramAutoSave = "OnProjectSave"
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, uc);
  be.seed("/proj/a.gb", gbRom());
  const id = systems.addSystem("/proj/a.gb")!; // savPath resolves to /proj/a.sav
  return { be, uc, systems, saver, id };
}

test("flushOnSave (OnProjectSave): writes the resolved <rom>.sav, then dedups", () => {
  const { be, saver, id } = setup();
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
  expect(saver.flushOnSave()).toBe(0); // on-disk matches live → seed, no rewrite
});

test("flushOnSave: a changed SRAM is rewritten", () => {
  const { be, saver, id } = setup();
  be.setSram(id, bytes(1, 2, 3));
  saver.flushOnSave();
  be.setSram(id, bytes(4, 5, 6));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile(SAV)!]).toEqual([4, 5, 6]);
});

test("Off: flushOnSave and pump are no-ops", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Off");
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.flushOnSave()).toBe(0);
  expect(saver.pump()).toBe(0);
  expect(be.readFile(SAV)).toBe(null); // nothing written
});

test("pump: writes only in Continuous, and only on change", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.pump()).toBe(1); // no file yet → write
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
  expect(saver.pump()).toBe(0); // unchanged (persistent hash) → no write
  be.setSram(id, bytes(7));
  expect(saver.pump()).toBe(1); // changed → write
  expect([...be.readFile(SAV)!]).toEqual([7]);
});

test("pump: seeds (no write) when an identical .sav is already on disk", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.setSram(id, bytes(5, 5));
  be.seed(SAV, bytes(5, 5)); // a just-loaded, identical sibling
  expect(saver.pump()).toBe(0); // first-obs matches → seed, no rewrite
});

test("embedded system (no romPath) is skipped", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, new UserConfigStore(be));
  systems.loadMgb(); // embedded, no romPath / sibling
  expect(saver.flushOnSave()).toBe(0);
  expect(be.log.includes("writeFile")).toBeFalsy();
});

test("a paired savPath override is honored as the write target", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, new UserConfigStore(be));
  be.seed("/proj/a.gb", gbRom());
  be.seed("/saves/custom.sav", bytes(0)); // a different paired save → becomes the override
  const id = systems.addSystem("/proj/a.gb", { explicitSav: "/saves/custom.sav" })!;
  be.setSram(id, bytes(1, 1));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile("/saves/custom.sav")!]).toEqual([1, 1]); // wrote to the override, not /proj/a.sav
  expect(be.readFile(SAV)).toBe(null);
});
