// The store classifies a ROM (platform.ts), and the platform picks the node type its core is placed as
// (systemNodeTypeFor): the node's C++ type is what builds that console's core, so the platform and core the
// entry carries and the node it is placed as are one decision. Embedded ROMs are always SameBoy.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { gbRom, nesRom, gbaRom } from "./fixtures";

function makeStore() {
  const be = new MockBackend("/cfg");
  return { be, store: new SystemsStore(be) };
}

test("a system's classified platform picks the node it is placed as, and the core its entry names", () => {
  const cases: Array<[string, Uint8Array, string, string, string]> = [
    ["/roms/a.gb", gbRom(), "rp.sameboy.gb", "gb", "sameboy"],
    ["/roms/a.nes", nesRom(), "rp.mesen.nes", "nes", "mesen"],
    ["/roms/a.gba", gbaRom(), "rp.mesen.gba", "gba", "mesen"],
  ];
  for (const [path, bytes, type, platform, core] of cases) {
    const { be, store } = makeStore();
    be.seed(path, bytes);
    expect(store.addSystem(path) != null).toBeTruthy();
    expect(be.lastBoot()!.type).toBe(type);
    expect(store.view()[0].platform).toBe(platform);
    expect(store.view()[0].core).toBe(core);
  }
});

test("embedded mGB is placed as a Game Boy node, from its marker", () => {
  const { be, store } = makeStore();
  store.loadMgb();
  const spec = be.lastBoot()!;
  expect(spec.type).toBe("rp.sameboy.gb");
  expect(spec.embeddedRom).toBe("mgb");
  expect(spec.romName).toBe("mgb"); // the marker names the ROM: an embedded one has no path to take a stem from
});
