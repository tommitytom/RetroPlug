// Chunk B: adopting a saved system passes its backend "system"-role config to constructSystem as the
// `settings` blob, so native applies a non-default model/highpass AT construct (not via a
// savestate-nuking restart). A fresh add sends no blob — the backend defaults already match the role
// schema. The store stays generic: it forwards whichever role's kind === the backend kind.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { gbRom, nesRom } from "../systems/fixtures";

function makeStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // the sameboy system role (model/highpass/linkGroupId/fastBoot)
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("adopt forwards the saved sameboy role config as the construct-time settings blob", () => {
  const { be, store } = makeStore();
  be.seed("/roms/a.gb", gbRom());

  // A saved system whose sameboy role carries a NON-default model (DmgB) + highpass (RemoveDcOffset).
  const roles = [{ kind: "sameboy", config: { model: "dmgB", highpass: "removeDcOffset", linkGroupId: 0, fastBoot: true } }];
  store.adopt({ romPath: "/roms/a.gb", roles });

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.settings != null).toBeTruthy();
  expect(JSON.parse(spec.settings!)).toEqual({ model: 1, highpass: 2, linkGroupId: 0, fastBoot: true }); // native-encoded blob
});

test("adopt forwards the saved mesen (NES) role config as the construct-time settings blob", () => {
  const { be, store } = makeStore();
  be.seed("/roms/a.nes", nesRom());

  // A saved NES system whose mesen role carries a non-default region (PAL=2). The store finds the
  // blob via r.kind === core, and a NES system's core is "mesen".
  const roles = [{ kind: "mesen", config: { region: "pal", removeSpriteLimit: true } }];
  store.adopt({ romPath: "/roms/a.nes", roles });

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.settings != null).toBeTruthy();
  expect(JSON.parse(spec.settings!)).toEqual({ region: 2, removeSpriteLimit: true }); // native-encoded blob
});

test("a fresh add sends no settings blob (backend defaults suffice)", () => {
  const { be, store } = makeStore();
  be.seed("/roms/b.gb", gbRom());

  store.addSystem("/roms/b.gb"); // fresh construct — defaultRoles run AFTER construct, so no blob
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.settings).toBe(undefined);
});
