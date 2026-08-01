// The lsdj-assets role: non-destructive kit/palette/font overrides stored in the project and applied to
// the base ROM at construct (onConstruct → spec.romBytes). Proven here through the REAL store path the
// menu uses (setRoleConfig + reloadSystem): the base .gb on disk is never written; the reload hands native
// a patched effective ROM. Byte-level correctness of the patch is the pure rom module's own tests; native
// honoring romBytes is test-native/lsdj-rom.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { LsdjRom, buildKitBank, encodeLsdpal, ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { gbRomBattery } from "./fixtures";

// A 1 MiB image LsdjRom accepts: GB logo + battery header (classifies as GB), a version title so
// identifyLsdj parses it, and a palette block (PALETTE_CHECK marker + a bank-27 names landmark sized to 2
// palettes) so palette overrides can be applied. Kits need no markers (importKitFile writes any slot).
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0); // logo @0x104 → classifies as GB
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  // Palette block: 2 palettes ending at the PALETTE_CHECK marker (bank 1).
  const count = 2;
  b.set(PALETTE_CHECK, 1 * BANK_SIZE + 0x100 + count * PALETTE_SIZE);
  // Bank-27 grayscale-names landmark: 3 font slots, then 2*count valid name slots → paletteCount = count.
  const nb = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3 + 2 * count; i++) for (let j = 0; j < 4; j++) b[nb + i * 5 + j] = 0x41 + j;
  b[nb + 15 + 2 * count * 5 + 4] = 0x01; // terminator
  return b;
}

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerLsdjAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("an LSDj system attaches an empty lsdj-assets role and constructs with no romBytes", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb");
  expect(id).toBeTruthy();
  expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-assets")).toBeTruthy();
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes).toBe(undefined); // no overrides → base ROM loaded from romPath
});

test("replacing a kit records an override and reload hands native the patched effective ROM", () => {
  const { be, store } = newStore();
  const base = lsdjRom1M();
  be.seed("/roms/song.gb", base);
  const id = store.addSystem("/roms/song.gb")!;

  // The menu's "Replace from Disk…" links to a .kit file on disk (never embeds bytes).
  const bank = buildKitBank("NEWK", [{ name: "BD", bytes: Uint8Array.of(0x12, 0x34, 0x56, 0x78) }]);
  be.seed("/kits/new.kit", bank);
  const overrides = [{ type: "kit", slot: 0, name: "NEWK", path: "/kits/new.kit" }];

  expect(store.setRoleConfig(id, "lsdj-assets", { overrides })).toBeTruthy();
  expect(store.reloadSystem(id)).toBeTruthy();

  // The reload constructed with a patched effective ROM (romBytes), NOT by touching romPath.
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = LsdjRom.fromBytes(spec.romBytes!);
  expect(patched.kit(0).name()).toBe("NEWK");
  expect([...patched.exportKitFile(0)]).toEqual([...bank]); // the exact bank we imported

  // Non-destructive: the on-disk .gb the mock holds is byte-unchanged.
  expect([...be.readFile("/roms/song.gb")!]).toEqual([...base]);
});

test("a palette override is stored INLINE (colorSets, no file path) and applied to the effective ROM", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;

  // The structured colours the menu derives from a .lsdpal (decodeLsdpal) — stored inline in the .rplg,
  // never a file link. Build them via encodeLsdpal→decode to mirror the real path.
  const colorSets = Array.from({ length: 5 }, () => ({
    colors: [{ r: 248, g: 0, b: 0 }, { r: 0, g: 248, b: 0 }, { r: 0, g: 0, b: 248 }, { r: 0, g: 0, b: 0 }],
  }));
  expect(encodeLsdpal("NEON", colorSets).length).toBe(4 + PALETTE_SIZE); // (sanity on the codec)
  const override = { type: "palette", slot: 1, name: "NEON", colorSets };

  store.setRoleConfig(id, "lsdj-assets", { overrides: [override] });
  store.reloadSystem(id);
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no path stored

  const patched = LsdjRom.fromBytes(spec.romBytes!);
  expect(patched.palettes()[1].name).toBe("NEON");
  expect(patched.palettes()[1].color(0, 0)).toEqual({ r: 255, g: 0, b: 0 }); // 248 → 5-bit 31 → 255
});

test("a kit erase override empties a base-valid kit slot in the effective ROM (Delete)", () => {
  const { be, store } = newStore();
  // A base ROM with a valid kit in slot 0 (so Delete records an erase override, not just a dropped one).
  const withKit = LsdjRom.fromBytes(lsdjRom1M());
  withKit.setKitBank(0, buildKitBank("OLDK", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }]));
  const base = withKit.bytes();
  be.seed("/roms/song.gb", base);
  const id = store.addSystem("/roms/song.gb")!;

  store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = LsdjRom.fromBytes(spec.romBytes!);
  expect(patched.kit(0).valid).toBe(false); // the slot is now empty
  expect([...be.readFile("/roms/song.gb")!]).toEqual([...base]); // on-disk .gb untouched
});

test("Remove Override reverts the next construct to the base ROM (no romBytes)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;

  const bank = buildKitBank("NEWK", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }]);
  be.seed("/kits/new.kit", bank);
  store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "NEWK", path: "/kits/new.kit" }] });
  const id2 = store.reloadSystem(id)!; // reload swaps the id in place
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();

  // Remove the override (menu's "Remove Override") → next construct is the base ROM again.
  store.setRoleConfig(id2, "lsdj-assets", { overrides: [] });
  store.reloadSystem(id2);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
});
