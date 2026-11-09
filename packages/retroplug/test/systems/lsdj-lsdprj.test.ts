// The lsdprjKit role path: a .lsdprj's bundled kit is recorded as an lsdj-assets override, and a reload folds it into the
// ROM the core boots. The pure planner (planLsdprjImport) and the batch importer are the LSDj package's own tests
// (packages/lsdj/ts/test/lsdprjImport.test.ts, songImport.test.ts); what stays here is the store end to end.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "@retroplug/core/systemRoles";
import { registerCoreRoles } from "../_coreRoles";
import { registerFeatureRoles } from "../_featureRoles";
import { registerLsdjAssetsRole } from "@retroplug/lsdj/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { encodeLsdprj, savFrom } from "@retroplug/lsdj";
import { LsdjRom, buildKitBank, ROM_SIZE } from "@retroplug/lsdj/rom";
import { gbRomBattery } from "./fixtures";

// A minimal 1 MiB image LsdjRom accepts (GB logo + a version title); all kit slots start free.
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

// A .lsdprj whose song uses kit index 5, carrying one custom kit bank ("DRUMS").
function lsdprjWithKit(): { file: Uint8Array; kit: Uint8Array } {
  const song = savFrom({ workingSong: { instruments: [{ type: "kit", kit1: 5, kit2: 5 }] } }).subarray(0, 0x8000);
  const kit = buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }]);
  return { file: encodeLsdprj({ name: "MYPRJ", version: 0x40, songBytes: song, kitBanks: [kit] }), kit };
}

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerFeatureRoles(reg);
  registerLsdjAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("a lsdprjKit override patches the effective ROM's kit at construct (via reload)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.seed("/p.lsdprj", lsdprjWithKit().file);

  store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/p.lsdprj", lsdprjKit: 0 }] });
  store.reloadSystem(id);

  const spec = be.lastBoot()!;
  expect(spec.romBytes != null).toBeTruthy();
  const patched = LsdjRom.fromBytes(spec.rom!.bytes);
  expect(patched.kit(0).valid).toBeTruthy();
  expect(patched.kit(0).name()).toBe("DRUMS"); // bank extracted from the .lsdprj by ordinal
  expect([...be.readFile("/roms/song.gb")!]).toEqual([...lsdjRom1M()]); // base .gb untouched
});
