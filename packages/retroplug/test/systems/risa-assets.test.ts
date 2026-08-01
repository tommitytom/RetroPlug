// The risa-assets role: non-destructive theme/font ROM overrides stored in the project and applied to the
// base ROM at construct (onConstruct → spec.romBytes). Proven through the REAL store path the menu uses
// (setRoleConfig + reloadSystem): the base .nes on disk is never written; the reload hands native a patched
// effective ROM. Byte-level correctness of the patch is test/risa/rom.test.ts; native honoring romBytes is
// test-native/risa-rom. Mirrors test/systems/lsdj-assets.test.ts.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerRisaRole } from "../../src/risaRole";
import { registerRisaAssetsRole } from "../../src/risaAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { RisaRom, decodeThemeFromRom } from "../../src/risa/rom";
import { risaRomFull } from "./fixtures";

const THEME_NEON = {
  name: "NEON",
  bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25",
};

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerRisaRole(reg);
  registerRisaAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("a risa system attaches an empty risa-assets role and constructs with no romBytes", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.nes", risaRomFull());
  const id = store.addSystem("/roms/song.nes");
  expect(id).toBeTruthy();
  expect(store.view()[0].roles.map((r) => r.kind).includes("risa-assets")).toBeTruthy();
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
});

test("a theme override is stored INLINE (no path) and applied to the effective ROM", () => {
  const { be, store } = newStore();
  const base = risaRomFull();
  be.seed("/roms/song.nes", base);
  const id = store.addSystem("/roms/song.nes")!;

  const override = { type: "theme", slot: 2, name: "NEON", theme: THEME_NEON };
  store.setRoleConfig(id, "risa-assets", { overrides: [override] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no file link

  const patched = RisaRom.fromBytes(spec.romBytes!);
  const back = decodeThemeFromRom(patched.getTheme(2)!.recordBytes, patched.getTheme(2)!.nameBytes);
  expect(back.name).toBe("NEON");
  expect(back.bg).toBe("0x0D");
  expect(back.selection).toBe("0x25");
  expect([...be.readFile("/roms/song.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a font override LINKS a .chr file by path and applies it to the effective ROM", () => {
  const { be, store } = newStore();
  const base = risaRomFull();
  be.seed("/roms/song.nes", base);
  const id = store.addSystem("/roms/song.nes")!;

  const bank = new Uint8Array(0x2000).fill(0x5a);
  be.seed("/fonts/new.chr", bank);
  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "font", slot: 1, name: "new", path: "/fonts/new.chr" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = RisaRom.fromBytes(spec.romBytes!);
  expect([...patched.getChrFontSlot(1)!]).toEqual([...bank]);
  expect([...be.readFile("/roms/song.nes")!]).toEqual([...base]);
});

test("a wrong-size .chr font override is skipped (leaves the ROM unpatched)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.nes", risaRomFull());
  const id = store.addSystem("/roms/song.nes")!;

  be.seed("/fonts/bad.chr", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "font", slot: 0, name: "bad", path: "/fonts/bad.chr" }] });
  store.reloadSystem(id);

  // The one override was skipped, so applyOverridesToRom returns a clone equal to base — but romBytes is
  // still set (the role produced a fresh buffer). The font slot must remain the base pattern.
  const spec = be.constructCalls[be.constructCalls.length - 1];
  const slot0 = RisaRom.fromBytes(spec.romBytes ?? be.readFile("/roms/song.nes")!).getChrFontSlot(0)!;
  expect(slot0[0]).toBe(0); // base slot-0 pattern byte (s*13+0 = 0), not 0x00-filled by a bad override
});

test("a kit override LINKS a .rkit bank by path and splices it (bank + mirror) into the effective ROM", () => {
  const { be, store } = newStore();
  const base = risaRomFull();
  be.seed("/roms/song.nes", base);
  const id = store.addSystem("/roms/song.nes")!;

  // A real populated 8 KB DMC bank: the fixture's own slot-0 "TEST" kit (what Export would produce).
  const bank = RisaRom.fromBytes(base).getKitBank(0)!;
  be.seed("/kits/drums.rkit", bank);
  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 5, name: "DRUMS", path: "/kits/drums.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = RisaRom.fromBytes(spec.romBytes!);
  expect([...patched.getKitBank(5)!]).toEqual([...bank]); // bank spliced verbatim
  expect(patched.isKitPopulated(5)).toBe(true);
  expect(patched.kits().some((k) => k.slot === 5)).toBe(true); // mirror consistent → a re-parse lists it
  expect([...be.readFile("/roms/song.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("an erase kit override empties the base kit slot in the effective ROM", () => {
  const { be, store } = newStore();
  const base = risaRomFull();
  be.seed("/roms/song.nes", base);
  const id = store.addSystem("/roms/song.nes")!;
  expect(RisaRom.fromBytes(base).isKitPopulated(0)).toBe(true); // the fixture has a base kit in slot 0

  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect(RisaRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false); // cleared, not linked
  expect([...be.readFile("/roms/song.nes")!]).toEqual([...base]);
});

test("a wrong-size .rkit kit override is skipped (leaves the base kit intact)", () => {
  const { be, store } = newStore();
  const base = risaRomFull();
  be.seed("/roms/song.nes", base);
  const id = store.addSystem("/roms/song.nes")!;

  be.seed("/kits/bad.rkit", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 0, name: "bad", path: "/kits/bad.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  const patched = RisaRom.fromBytes(spec.romBytes ?? be.readFile("/roms/song.nes")!);
  expect(patched.isKitPopulated(0)).toBe(true); // base kit intact, not zeroed by a bad override
});

test("Remove Override reverts the next construct to the base ROM (no romBytes)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.nes", risaRomFull());
  const id = store.addSystem("/roms/song.nes")!;

  store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "theme", slot: 0, name: "NEON", theme: THEME_NEON }] });
  const id2 = store.reloadSystem(id)!; // reload swaps the id in place
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();

  store.setRoleConfig(id2, "risa-assets", { overrides: [] });
  store.reloadSystem(id2);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
});
