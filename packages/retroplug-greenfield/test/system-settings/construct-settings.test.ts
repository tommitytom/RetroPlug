// Chunk B: adopting a saved system passes its backend "system"-role config to constructSystem as the
// `settings` blob, so native applies a non-default model/highpass AT construct (not via a
// savestate-nuking restart). A fresh add sends no blob — the backend defaults already match the role
// schema. The store stays generic: it forwards whichever role's kind === the backend kind.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { gbRom } from "../systems/fixtures";

function makeStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // the sameboy system role (model/highpass/linkGroupId/fastBoot)
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("adopt forwards the saved sameboy role config as the construct-time settings blob", () => {
  const { be, store } = makeStore();
  be.seed("/roms/a.gb", gbRom());

  // A saved system whose sameboy role carries a NON-default model (DmgB=1) + highpass (RemoveDcOffset=2).
  const roles = [{ kind: "sameboy", config: { model: 1, highpass: 2, linkGroupId: 0, fastBoot: true } }];
  store.adopt({ romPath: "/roms/a.gb", roles });

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.settings != null).toBeTruthy();
  expect(JSON.parse(spec.settings!)).toEqual({ model: 1, highpass: 2, linkGroupId: 0, fastBoot: true });
});

test("a fresh add sends no settings blob (backend defaults suffice)", () => {
  const { be, store } = makeStore();
  be.seed("/roms/b.gb", gbRom());

  store.addSystem("/roms/b.gb"); // fresh construct — defaultRoles run AFTER construct, so no blob
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.settings).toBe(undefined);
});
