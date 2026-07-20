// The evermidi-assets role: non-destructive DMC-kit / CHR-font ROM overrides stored in the project and
// applied to the base ROM at construct (onConstruct → spec.romBytes). Proven through the REAL store path the
// menu uses (setRoleConfig + reloadSystem): the base .nes on disk is never written; the reload hands native a
// patched effective ROM. Byte-level correctness is test/evermidi/rom.test.ts; native honoring romBytes is
// test-native/evermidi-rom. Mirrors test/systems/risa-assets.test.ts (minus themes — EverMIDI has none yet).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerEverMidiRole } from "../../src/evermidiRole";
import { registerEverMidiAssetsRole } from "../../src/evermidiAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { EverMidiRom } from "../../src/evermidi/rom";
import { decodeThemeFromRom } from "../../src/risa/rom";
import { everMidiRom } from "./fixtures";

const THEME_NEON = {
  name: "NEON",
  bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25",
};

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerEverMidiRole(reg);
  registerEverMidiAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

/** A distinct populated 8 KB DMC bank (0xA5 magic) — what a real .rkit / Export produces. */
function populatedBank(fill: number): Uint8Array {
  const bank = new Uint8Array(0x2000).fill(fill);
  bank[0x1f40] = 0xa5;
  return bank;
}

test("an EverMIDI system attaches an empty evermidi-assets role and constructs with no romBytes", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", everMidiRom());
  const id = store.addSystem("/roms/synth.nes");
  expect(id).toBeTruthy();
  expect(store.view()[0].roles.map((r) => r.kind).includes("evermidi-assets")).toBeTruthy();
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
});

test("a theme override is stored INLINE (no path) and applied to the effective ROM", () => {
  const { be, store } = newStore();
  const base = everMidiRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const override = { type: "theme", slot: 0, name: "NEON", theme: THEME_NEON };
  store.setRoleConfig(id, "evermidi-assets", { overrides: [override] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no file link

  const patched = EverMidiRom.fromBytes(spec.romBytes!);
  const back = decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes);
  expect(back.name).toBe("NEON");
  expect(back.selection).toBe("0x25");
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a kit override LINKS a .rkit bank by path and splices it into the effective ROM", () => {
  const { be, store } = newStore();
  const base = everMidiRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const bank = populatedBank(0x11);
  be.seed("/kits/drums.rkit", bank);
  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  const patched = EverMidiRom.fromBytes(spec.romBytes!);
  expect([...patched.getKitBank(0)!]).toEqual([...bank]); // bank spliced verbatim
  expect(patched.isKitPopulated(0)).toBe(true);
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]); // on-disk .nes untouched
});

test("a font override LINKS a .chr file by path and applies it to the effective ROM", () => {
  const { be, store } = newStore();
  const base = everMidiRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  const bank = new Uint8Array(0x2000).fill(0x5a);
  be.seed("/fonts/new.chr", bank);
  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "font", slot: 0, name: "new", path: "/fonts/new.chr" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect([...EverMidiRom.fromBytes(spec.romBytes!).getChrFontSlot(0)!]).toEqual([...bank]);
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
});

test("a wrong-size .chr font override is skipped (leaves the base font intact)", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", everMidiRom());
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/fonts/bad.chr", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "font", slot: 0, name: "bad", path: "/fonts/bad.chr" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  const slot0 = EverMidiRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!).getChrFontSlot(0)!;
  expect(slot0[0]).toBe((0 * 7 + 3) & 0xff); // base font pattern, not 0x00-filled by a bad override
});

test("an erase kit override empties the base kit slot in the effective ROM", () => {
  const { be, store } = newStore();
  const base = everMidiRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;
  expect(EverMidiRom.fromBytes(base).isKitPopulated(0)).toBe(true); // the fixture has a base kit

  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romBytes != null).toBeTruthy();
  expect(EverMidiRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false); // cleared
  expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
});

test("a wrong-size .rkit kit override is skipped (leaves the base kit intact)", () => {
  const { be, store } = newStore();
  be.seed("/roms/synth.nes", everMidiRom());
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/kits/bad.rkit", new Uint8Array(0x1000)); // half a bank — rejected by applyOne
  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "kit", slot: 0, name: "bad", path: "/kits/bad.rkit" }] });
  store.reloadSystem(id);

  const spec = be.constructCalls[be.constructCalls.length - 1];
  const patched = EverMidiRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!);
  expect(patched.isKitPopulated(0)).toBe(true); // base kit intact, not zeroed by a bad override
});

test("Remove Override reverts the next construct to the base ROM (no romBytes)", () => {
  const { be, store } = newStore();
  const base = everMidiRom();
  be.seed("/roms/synth.nes", base);
  const id = store.addSystem("/roms/synth.nes")!;

  be.seed("/kits/drums.rkit", populatedBank(0x22));
  store.setRoleConfig(id, "evermidi-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
  const id2 = store.reloadSystem(id)!; // reload swaps the id in place
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();

  store.setRoleConfig(id2, "evermidi-assets", { overrides: [] });
  store.reloadSystem(id2);
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
});
