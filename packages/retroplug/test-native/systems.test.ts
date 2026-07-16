// SystemsStore lifecycle over the REAL native Backend — now backed by a real SameBoySystem
// (a genuine Game Boy core), not a stub. constructSystem boots the core; readState/readSram
// return real savestate/SRAM. Assertions are structural (real bytes are non-empty but not a
// fixed value) and on returned id handles, never absolute ids: every case in a file shares one
// native Project process, but a fresh SystemsStore starts empty (it tracks its own list).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { SystemsStore } from "../src/systemsStore";
import { gbRomBattery, garbage } from "../test/systems/fixtures";

declare const __CONFIG_DIR__: string;

test("add a file-backed ROM → a live Game Boy whose pump returns real savestate + SRAM", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/a.gb";
  be.writeFile(rom, gbRomBattery());

  const store = new SystemsStore(be);
  const id = store.addSystem(rom)!;
  expect(typeof id).toBe("number");
  expect(store.systems().length).toBe(1);

  // Real core: a full savestate, and battery SRAM (the fixture is a battery cart).
  const state = be.readState(id)!;
  const sram = be.readSram(id)!;
  expect(state.length > 0).toBeTruthy();
  expect(sram.length > 0).toBeTruthy();
});

test("duplicate → remove → reload, all against a real Project of Game Boys", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/b.gb";
  be.writeFile(rom, gbRomBattery());

  const store = new SystemsStore(be);
  const a = store.addSystem(rom)!;
  const b = store.duplicateSystem(a)!;
  expect(store.systems().length).toBe(2);
  expect(be.readState(b) != null).toBeTruthy(); // the clone is a live independent core

  expect(store.removeSystem(b)).toBeTruthy();
  expect(store.systems().length).toBe(1);
  expect(be.readState(b)).toBe(null); // dropped from the Project → the pull misses

  const a2 = store.reloadSystem(a)!;
  expect(store.systems().length).toBe(1);
  expect(a2 !== a).toBeTruthy(); // reload swaps in a fresh id
  expect(be.readState(a2) != null).toBeTruthy();
});

test("mGB (embedded) boots with no ROM file; a garbage file is rejected", () => {
  const be = createRealBackend();
  const store = new SystemsStore(be);

  const mgb = store.loadMgb()!; // embedded → rp::embeddedRom("mgb") boots a real core
  expect(store.systems().length).toBe(1);
  expect(be.readState(mgb)!.length > 0).toBeTruthy(); // real mGB savestate

  const bad = __CONFIG_DIR__ + "/roms/bad.bin";
  be.writeFile(bad, garbage());
  expect(store.addSystem(bad)).toBe(null); // classify gate rejects the unknown format
  expect(store.systems().length).toBe(1); // nothing built
});
