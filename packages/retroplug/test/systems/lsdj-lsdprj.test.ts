// .lsdprj import: the pure planner (planLsdprjImport — song injected byte-level + missing kits assigned to
// free slots, linking the .lsdprj by path+ordinal) and the role path (a lsdprjKit override patches the
// effective ROM at construct through the store's reload).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { planLsdprjImport } from "../../src/lsdjLsdprjImport";
import { decodeSav, encodeLsdprj, savFrom } from "../../src/lsdjSav";
import { LsdjRom, buildKitBank, ROM_SIZE } from "../../src/lsdj/rom";
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

test("planLsdprjImport assigns a missing kit to a free slot + injects the song", () => {
  const { file } = lsdprjWithKit();
  const plan = planLsdprjImport({ file, path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: savFrom({}) })!;
  expect(plan != null).toBeTruthy();
  expect(plan.addedKits).toBe(1);
  expect(plan.songSlot).toBe(0);
  const ov = plan.overrides[0];
  expect(ov.type).toBe("kit");
  expect(ov.path).toBe("/p.lsdprj");
  expect(ov.lsdprjKit).toBe(0);
  expect(ov.name).toBe("DRUMS");
  expect(decodeSav(plan.savBytes).projects[0]!.name).toBe("MYPRJ"); // song injected
});

test("planLsdprjImport dedupes a kit already present in the ROM (no new override)", () => {
  const { file, kit } = lsdprjWithKit();
  const rom = LsdjRom.fromBytes(lsdjRom1M());
  rom.importKitFile(3, kit); // the same kit already lives at slot 3
  const plan = planLsdprjImport({ file, path: "/p.lsdprj", effectiveRom: rom.bytes(), overrides: [], liveSram: savFrom({}) })!;
  expect(plan.addedKits).toBe(0); // reused slot 3, no new override
  expect(plan.overrides.length).toBe(0);
});

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
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

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = LsdjRom.fromBytes(spec.romBytes!);
  expect(patched.kit(0).valid).toBeTruthy();
  expect(patched.kit(0).name()).toBe("DRUMS"); // bank extracted from the .lsdprj by ordinal
  expect([...be.readFile("/roms/song.gb")!]).toEqual([...lsdjRom1M()]); // base .gb untouched
});
