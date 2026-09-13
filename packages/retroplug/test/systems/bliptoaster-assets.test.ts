// The bliptoaster-assets role: non-destructive DMC-kit / CHR-font ROM overrides stored in the project and
// applied to the base ROM at construct (onConstruct → spec.romBytes). Proven through the REAL store path the
// menu uses (setRoleConfig + reloadSystem): the base .nes on disk is never written; the reload hands native a
// patched effective ROM. Byte-level correctness is test/bliptoaster/rom.test.ts; native honoring romBytes is
// test-native/bliptoaster-rom. Mirrors test/systems/risa-assets.test.ts (minus themes — BlipToaster has none yet).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerBlipToasterRole } from "../../src/bliptoasterRole";
import { registerBlipToasterAssetsRole } from "../../src/bliptoasterAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { BlipToasterRom } from "../../src/bliptoaster/rom";
import { decodeThemeFromRom } from "../../src/risa/rom";
import { blipToasterRom, blipToasterMultiKitRom } from "./fixtures";

const THEME_NEON = {
  name: "NEON",
  bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25",
};

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerBlipToasterRole(reg);
  registerBlipToasterAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

/** A distinct populated 8 KB DMC bank (0xA5 magic) — what a real .rkit / Export produces. */
function populatedBank(fill: number): Uint8Array {
  const bank = new Uint8Array(0x2000).fill(fill);
  bank[0x1f40] = 0xa5;
  return bank;
}

test("a BlipToaster system attaches an empty bliptoaster-assets role and constructs with no romBytes", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes");
  expect(id).toBeTruthy();
  expect(store.view()[0].roles.map((r) => r.kind).includes("bliptoaster-assets")).toBeTruthy();
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
});

// ── The baked rig settings (the same role config, a sibling key to `overrides`) ────────────────────────
// The cart has no settings memory, so the screen/rig defaults are bytes in the ROM's code bank. RetroPlug pins
// only the fields the user changed and folds them in at construct, exactly like an asset override.

/** The effective ROM's decoded settings after the latest construct (null when construct got no romBytes). */
function constructedSettings(be: MockBackend) {
  const spec = be.constructCalls[be.constructCalls.length - 1];
  return spec.romBytes ? BlipToasterRom.fromBytes(spec.romBytes).settings() : null;
}

test("a settings patch is folded into the effective ROM, and leaves every field it does not name alone", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;
  // The fixture's block is the one a fresh build ships: format 1, every field at its power-on default.
  expect(BlipToasterRom.fromBytes(base).settings()).toEqual({ baseChannel: 0, kit: 0, ppu: true, velCurve: false, theme: 0, font: 0 });

  store.setRoleConfig(id, "bliptoaster-assets", { settings: { theme: 11, font: 2 } });
  store.reloadSystem(id);
  // Only the two named fields moved - the rest are still the ROM's own bytes, not defaults written back over them.
  expect(constructedSettings(be)).toEqual({ baseChannel: 0, kit: 0, ppu: true, velCurve: false, theme: 11, font: 2 });
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("settings and asset overrides ride the same config and are both applied", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes")!;
  store.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [{ type: "theme", slot: 0, name: "NEON", theme: THEME_NEON }],
    settings: { baseChannel: 3, kit: 5, ppu: false, velCurve: true },
  });
  store.reloadSystem(id);

  const patched = BlipToasterRom.fromBytes(be.constructCalls[be.constructCalls.length - 1].romBytes!);
  expect(patched.settings()).toEqual({ baseChannel: 3, kit: 5, ppu: false, velCurve: true, theme: 0, font: 0 });
  expect(decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes).name).toBe("NEON");
});

test("a settings field written on its own still triggers the patch (an empty override list is not 'nothing to do')", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes")!;
  store.setRoleConfig(id, "bliptoaster-assets", { settings: { ppu: false } });
  store.reloadSystem(id);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();
  expect(constructedSettings(be)!.ppu).toBe(false);
});

test("clearing the settings patch reverts the next construct to the base ROM (no romBytes)", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes")!;
  store.setRoleConfig(id, "bliptoaster-assets", { settings: { theme: 7 } });
  const id2 = store.reloadSystem(id)!; // reload swaps the id in place
  expect(constructedSettings(be)!.theme).toBe(7);

  store.setRoleConfig(id2, "bliptoaster-assets", { settings: {} });
  store.reloadSystem(id2);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // nothing pinned → base ROM
});

test("a ROM with no readable settings block takes the asset overrides and ignores the settings patch", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  base.fill(0, 0x100 + 6 + 16 * 11, 0x100 + 6 + 16 * 11 + 6); // wipe the SETT magic, leave the theme table
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;
  store.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [{ type: "theme", slot: 0, name: "NEON", theme: THEME_NEON }],
    settings: { theme: 11 },
  });
  store.reloadSystem(id);

  const patched = BlipToasterRom.fromBytes(be.constructCalls[be.constructCalls.length - 1].romBytes!);
  expect(patched.hasSettings).toBe(false);
  expect(patched.settings()).toBe(null);
  expect(decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes).name).toBe("NEON");
});

test("a settings block stamped a format this build does not read is left entirely alone", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  const at = 0x100 + 6 + 16 * 11; // the block, immediately after the theme table
  base[at + 6] = 2; // a format from some later tool
  const before = base.slice();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;
  store.setRoleConfig(id, "bliptoaster-assets", { settings: { theme: 11, ppu: false } });
  store.reloadSystem(id);

  // Half-writing a layout we do not know would be worse than doing nothing, so construct hands over an
  // untouched image rather than a partly-patched one.
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect([...(spec.romBytes ?? before)]).toEqual([...before]);
});

test("a theme override is stored INLINE (no path) and applied to the effective ROM", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const override = { type: "theme", slot: 0, name: "NEON", theme: THEME_NEON };
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [override] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no file link

  const patched = BlipToasterRom.fromBytes(spec.romBytes!);
  const back = decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes);
  expect(back.name).toBe("NEON");
  expect(back.selection).toBe("0x25");
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a kit override LINKS a .rkit bank by path and splices it into the effective ROM", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const bank = populatedBank(0x11);
  be.seed("/kits/drums.rkit", bank);
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = BlipToasterRom.fromBytes(spec.romBytes!);
  expect([...patched.getKitBank(0)!]).toEqual([...bank]); // bank spliced verbatim
  expect(patched.isKitPopulated(0)).toBe(true);
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a banking ROM takes a kit override into a high slot; the base kit is preserved", () => {
  const { be, store } = newStore();
  const base = blipToasterMultiKitRom(); // mapper 69 (FME-7) → 16 switchable kit banks
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const bank = populatedBank(0x33);
  be.seed("/kits/hats.rkit", bank);
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 5, name: "HATS", path: "/kits/hats.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = BlipToasterRom.fromBytes(spec.romBytes!);
  expect([...patched.getKitBank(5)!]).toEqual([...bank]); // spliced into bank 5
  expect(patched.kits().map((k) => k.slot)).toEqual([0, 5]); // base slot 0 kept, slot 5 added
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a font override LINKS a .chr file by path and applies it to the effective ROM", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const bank = new Uint8Array(0x2000).fill(0x5a);
  be.seed("/fonts/new.chr", bank);
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "font", slot: 0, name: "new", path: "/fonts/new.chr" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect([...BlipToasterRom.fromBytes(spec.romBytes!).getChrFontSlot(0)!]).toEqual([...bank]);
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
});

test("a wrong-size .chr font override is skipped (leaves the base font intact)", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/fonts/bad.chr", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "font", slot: 0, name: "bad", path: "/fonts/bad.chr" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  const slot0 = BlipToasterRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!).getChrFontSlot(0)!;
  expect(slot0[0]).toBe((0 * 7 + 3) & 0xff); // base font pattern, not 0x00-filled by a bad override
});

test("an erase kit override empties the base kit slot in the effective ROM", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;
  expect(BlipToasterRom.fromBytes(base).isKitPopulated(0)).toBe(true); // the fixture has a base kit

  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect(BlipToasterRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false); // cleared
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
});

test("a wrong-size .rkit kit override is skipped (leaves the base kit intact)", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", blipToasterRom());
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/kits/bad.rkit", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "bad", path: "/kits/bad.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  const patched = BlipToasterRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!);
  expect(patched.isKitPopulated(0)).toBe(true); // base kit intact, not zeroed by a bad override
});

test("Remove Override reverts the next construct to the base ROM (no romBytes)", () => {
  const { be, store } = newStore();
  const base = blipToasterRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/kits/drums.rkit", populatedBank(0x22));
  store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
  const id2 = store.reloadSystem(id)!; // reload swaps the id in place
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();

  store.setRoleConfig(id2, "bliptoaster-assets", { overrides: [] });
  store.reloadSystem(id2);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
});
