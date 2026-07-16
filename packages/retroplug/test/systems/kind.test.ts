// The store classifies a ROM (platform.ts) and passes that platform + core to constructSystem, so native
// routes it to the right backend (sameboy / mesen-nes / mesen-gba). Embedded ROMs are always SameBoy.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom, nesRom, gbaRom } from "./fixtures";

function makeStore() {
  const be = new MockBackend("/cfg");
  return { be, store: new SystemsStore(be) };
}

test("constructSystem carries the classified platform and core for each backend", () => {
  const cases: Array<[string, Uint8Array, string, string]> = [
    ["/roms/a.gb", gbRom(), "gb", "sameboy"],
    ["/roms/a.nes", nesRom(), "nes", "mesen"],
    ["/roms/a.gba", gbaRom(), "gba", "mesen"],
  ];
  for (const [path, bytes, platform, core] of cases) {
    const { be, store } = makeStore();
    be.seed(path, bytes);
    expect(store.addSystem(path) != null).toBeTruthy();
    const spec = be.constructCalls[be.constructCalls.length - 1];
    expect(spec.platform).toBe(platform);
    expect(spec.core).toBe(core);
  }
});

test("embedded mGB constructs as sameboy", () => {
  const { be, store } = makeStore();
  store.loadMgb();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.platform).toBe("gb");
  expect(spec.core).toBe("sameboy");
});
