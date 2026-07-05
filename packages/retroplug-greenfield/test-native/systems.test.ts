// SystemsStore lifecycle over the REAL native Backend: a StubSystem in a real Project.
// constructSystem / duplicate / reload / remove and the read pump (readSram/readState) all
// run over the host binary — the first time the systems orchestration touches native code.
// Assertions are on observable outcomes (returned id handles + the pump's bytes), never on
// absolute ids: every case in a file shares one native Project process, so ids are monotonic
// across the whole file — a fresh SystemsStore still starts empty (it tracks its own list).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { SystemsStore } from "../src/systemsStore";
import { gbRom, garbage } from "../test/systems/fixtures";

declare const __CONFIG_DIR__: string;

test("add a file-backed ROM → a live system whose pump returns the stub's tagged bytes", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/a.gb";
  be.writeFile(rom, gbRom());

  const store = new SystemsStore(be);
  const id = store.addSystem(rom)!;
  expect(typeof id).toBe("number");
  expect(store.systems().length).toBe(1);

  // The stub publishes deterministic id-tagged bytes: "SR"/"ST" (0x53,0x52 / 0x53,0x54).
  expect(Array.from(be.readSram(id)!.slice(0, 2))).toEqual([0x53, 0x52]);
  expect(Array.from(be.readState(id)!.slice(0, 2))).toEqual([0x53, 0x54]);
});

test("duplicate → remove → reload, all against the real Project", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/b.gb";
  be.writeFile(rom, gbRom());

  const store = new SystemsStore(be);
  const a = store.addSystem(rom)!;
  const b = store.duplicateSystem(a)!;
  expect(store.systems().length).toBe(2);
  expect(be.readSram(b) != null).toBeTruthy(); // the clone is live in native

  expect(store.removeSystem(b)).toBeTruthy();
  expect(store.systems().length).toBe(1);
  expect(be.readSram(b)).toBe(null); // dropped from the Project → the pull misses

  const a2 = store.reloadSystem(a)!;
  expect(store.systems().length).toBe(1);
  expect(a2 !== a).toBeTruthy(); // reload swaps in a fresh id
  expect(be.readSram(a2) != null).toBeTruthy();
});

test("mGB (embedded) builds with no ROM file; a garbage file is rejected TS-side", () => {
  const be = createRealBackend();
  const store = new SystemsStore(be);

  const mgb = store.loadMgb()!; // embedded → adopts into the empty store (no slurp)
  expect(store.systems().length).toBe(1);
  expect(be.readState(mgb) != null).toBeTruthy();

  const bad = __CONFIG_DIR__ + "/roms/bad.bin";
  be.writeFile(bad, garbage());
  expect(store.addSystem(bad)).toBe(null); // classify gate rejects the unknown format
  expect(store.systems().length).toBe(1); // nothing built
});
