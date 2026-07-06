// The store classifies a ROM (romFormat.ts) and passes that SystemKind to constructSystem, so native
// routes it to the right backend (sameboy / mesen-nes / mesen-gba). Embedded ROMs are always SameBoy.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom, nesRom, gbaRom } from "./fixtures";

function makeStore() {
  const be = new MockBackend("/cfg");
  return { be, store: new SystemsStore(be) };
}

test("constructSystem carries the classified kind for each backend", () => {
  const cases: Array<[string, Uint8Array, string]> = [
    ["/roms/a.gb", gbRom(), "sameboy"],
    ["/roms/a.nes", nesRom(), "nes"],
    ["/roms/a.gba", gbaRom(), "gba"],
  ];
  for (const [path, bytes, kind] of cases) {
    const { be, store } = makeStore();
    be.seed(path, bytes);
    expect(store.addSystem(path) != null).toBeTruthy();
    const spec = be.constructCalls[be.constructCalls.length - 1];
    expect(spec.kind).toBe(kind);
  }
});

test("embedded mGB constructs as sameboy", () => {
  const { be, store } = makeStore();
  store.loadMgb();
  expect(be.constructCalls[be.constructCalls.length - 1].kind).toBe("sameboy");
});
