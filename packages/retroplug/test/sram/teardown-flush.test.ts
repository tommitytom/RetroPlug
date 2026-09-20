// Closing a cart must not lose its battery. Every path that destroys a core flushes the live battery to
// its own `.sav` first, so Remove Instance / Replace / Load SRAM / Load State / Reset / a ROM-watch reload
// no longer discard whatever the cart has played since the last save.
//
// The exception is the reason this file exists: a caller that has just WRITTEN a .sav and is cold-booting
// from it (a Songs-menu edit, a song import) leaves the live battery holding the PRE-edit bytes, so
// flushing there would undo the save. That case is pinned at the bottom.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRomBattery } from "../systems/fixtures";

const bytes = (...b: number[]) => new Uint8Array(b);
const ROM = "/roms/a.gb";
const SAV = "/roms/a.sav";

/** A store with one battery cart whose live battery differs from (absent) disk. */
function dirtyCart() {
  const be = new MockBackend("/cfg");
  be.seed(ROM, gbRomBattery());
  const store = new SystemsStore(be);
  const id = store.addSystem(ROM)!;
  be.setSram(id, bytes(1, 2, 3));
  expect(be.readFile(SAV)).toBe(null); // nothing on disk yet
  return { be, store, id };
}

test("removeSystem writes the battery before the core is dropped", () => {
  const { be, store, id } = dirtyCart();
  expect(store.removeSystem(id)).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
});

test("clear (New/Load Project) writes every cart's battery", () => {
  const { be, store, id } = dirtyCart();
  be.seed("/roms/b.gb", gbRomBattery());
  const b = store.addSystem("/roms/b.gb")!;
  be.setSram(b, bytes(4, 5));
  store.clear();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
  expect([...be.readFile("/roms/b.sav")!]).toEqual([4, 5]);
  expect(id !== b).toBeTruthy();
});

test("replaceSystem writes the OUTGOING cart's battery, to its own .sav", () => {
  const { be, store, id } = dirtyCart();
  be.seed("/roms/b.gb", gbRomBattery());
  expect(store.replaceSystem(id, "/roms/b.gb") !== null).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]); // the cart that was closed
});

test("swapRom writes the OLD ROM's .sav before carrying the battery to the new cart", () => {
  const { be, store, id } = dirtyCart();
  be.seed("/roms/b.gb", gbRomBattery());
  expect(store.swapRom(id, "/roms/b.gb") !== null).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]); // old target no longer left stale
});

test("reset and reloadSystem write the battery they claim to carry forward", () => {
  const r = dirtyCart();
  expect(r.store.reset(r.id) !== null).toBeTruthy();
  expect([...r.be.readFile(SAV)!]).toEqual([1, 2, 3]);

  const l = dirtyCart();
  expect(l.store.reloadSystem(l.id) !== null).toBeTruthy();
  expect([...l.be.readFile(SAV)!]).toEqual([1, 2, 3]);
});

test("loadState writes the battery the savestate is about to replace", () => {
  const { be, store, id } = dirtyCart();
  be.seed("/states/x.state", bytes(9, 9));
  expect(store.loadState(id, "/states/x.state") !== null).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
});

test("loadSram writes the battery to the OLD target before booting the picked file", () => {
  const { be, store, id } = dirtyCart();
  be.seed("/saves/other.sav", bytes(7, 7, 7));
  expect(store.loadSram(id, "/saves/other.sav") !== null).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]); // the cart's own sav kept its work
  expect([...be.readFile("/saves/other.sav")!]).toEqual([7, 7, 7]); // the picked file is untouched
});

// THE TRAP. A Songs-menu edit writes the mutated sav and then cold-boots from it, so at the moment of
// loadSram the LIVE battery is the pre-edit copy. Flushing it would write the old song back over the new
// one - the fix causing the exact loss it exists to prevent.
test("loadSram does NOT flush when the file being loaded IS the cart's own target", () => {
  const { be, store, id } = dirtyCart();
  be.writeFile(SAV, bytes(4, 5, 6)); // the caller just saved an edit here
  expect(store.loadSram(id, SAV) !== null).toBeTruthy();
  expect([...be.readFile(SAV)!]).toEqual([4, 5, 6]); // the edit survived; NOT the live [1,2,3]
});

test("a cart with nothing new to save writes nothing at all", () => {
  const { be, store, id } = dirtyCart();
  be.writeFile(SAV, bytes(1, 2, 3)); // disk already matches the live battery
  const before = be.log.length;
  expect(store.removeSystem(id)).toBeTruthy();
  expect(be.log.slice(before).includes("writeFileAtomic")).toBeFalsy();
});
