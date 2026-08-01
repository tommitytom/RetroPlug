// The pure asset-catalog layer: integration/asset resolution by the tracker marker role, the generic
// effective-asset merge (overlay / erase / add / name precedence / sort), and each console's baseSlots parse
// over a fixture ROM. The deep ROM parse (kits/palettes/fonts) is covered by test/{lsdj,risa}/rom.test.ts.
import { test, expect } from "../../testing/harness";
import {
  resolveTracker,
  resolveAssetCatalog,
  effectiveAssets,
  lsdjAssetCatalog,
  risaAssetCatalog,
  type AssetSlot,
} from "../../src/tracker";
import { ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { risaRomFull, gbRomBattery } from "../systems/fixtures";

// A 1 MiB image LsdjRom accepts (mirrors systems/lsdj-assets.test): GB logo + battery header, a version
// title, and a 2-palette block (PALETTE_CHECK marker + a bank-27 names landmark).
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  const count = 2;
  b.set(PALETTE_CHECK, 1 * BANK_SIZE + 0x100 + count * PALETTE_SIZE);
  const nb = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3 + 2 * count; i++) for (let j = 0; j < 4; j++) b[nb + i * 5 + j] = 0x41 + j;
  b[nb + 15 + 2 * count * 5 + 4] = 0x01;
  return b;
}

test("resolveAssetCatalog selects the console's asset catalog by its tracker marker role", () => {
  expect(resolveTracker([{ kind: "lsdj-sync", config: {} }])?.assets).toBe(lsdjAssetCatalog);
  expect(resolveAssetCatalog([{ kind: "lsdj-sync", config: {} }, { kind: "lsdj-assets", config: {} }])).toBe(lsdjAssetCatalog);
  expect(resolveAssetCatalog([{ kind: "risa", config: {} }, { kind: "risa-assets", config: {} }])).toBe(risaAssetCatalog);
  expect(resolveAssetCatalog([{ kind: "mesen", config: {} }])).toBe(undefined);
  expect(lsdjAssetCatalog.assetRole).toBe("lsdj-assets");
  expect(risaAssetCatalog.assetRole).toBe("risa-assets");
  expect(lsdjAssetCatalog.types.map((t) => t.kind)).toEqual(["kit", "font", "palette"]);
  expect(risaAssetCatalog.types.map((t) => t.kind)).toEqual(["theme", "font", "kit"]);
  // only kits are addable (Add.../Delete); the colour + font types are a fixed base-slot list
  expect(lsdjAssetCatalog.types.filter((t) => t.addable).map((t) => t.kind)).toEqual(["kit"]);
  expect(risaAssetCatalog.types.filter((t) => t.addable).map((t) => t.kind)).toEqual(["kit"]);
});

test("effectiveAssets overlays overrides on base slots: replace marks *, add appends, erase removes, sorted", () => {
  const base: AssetSlot[] = [{ slot: 0, name: "A" }, { slot: 2, name: "C" }];
  expect(effectiveAssets(base, [], "kit", "Kit")).toEqual([
    { slot: 0, name: "A", overridden: false },
    { slot: 2, name: "C", overridden: false },
  ]);
  // a replace override marks the slot overridden + takes its name; an add override appends a fresh slot
  expect(effectiveAssets(base, [{ type: "kit", slot: 0, name: "X" }, { type: "kit", slot: 1, name: "New" }], "kit", "Kit")).toEqual([
    { slot: 0, name: "X", overridden: true },
    { slot: 1, name: "New", overridden: true },
    { slot: 2, name: "C", overridden: false },
  ]);
  // an erase override removes the slot
  expect(effectiveAssets(base, [{ type: "kit", slot: 2, erase: true }], "kit", "Kit").map((r) => r.slot)).toEqual([0]);
  // overrides of another type are ignored
  expect(effectiveAssets(base, [{ type: "font", slot: 0, name: "Z" }], "kit", "Kit")).toEqual([
    { slot: 0, name: "A", overridden: false },
    { slot: 2, name: "C", overridden: false },
  ]);
});

test("effectiveAssets name precedence: override name, else base name, else `${noun} ${slot}`", () => {
  expect(effectiveAssets([{ slot: 0, name: "Base" }], [{ type: "kit", slot: 0, name: "Over" }], "kit", "Kit")[0].name).toBe("Over");
  expect(effectiveAssets([{ slot: 0, name: "Base" }], [{ type: "kit", slot: 0, name: "" }], "kit", "Kit")[0].name).toBe("Base");
  expect(effectiveAssets([], [{ type: "kit", slot: 5, name: "" }], "kit", "Kit")[0].name).toBe("Kit 5");
});

test("lsdjAssetCatalog.baseSlots parses the base ROM's palettes/kits (empty for a non-LSDj image)", () => {
  const rom = lsdjRom1M();
  expect(lsdjAssetCatalog.baseSlots(rom, "palette").map((s) => s.slot)).toEqual([0, 1]);
  expect(lsdjAssetCatalog.baseSlots(rom, "kit")).toEqual([]); // no base kits in the synthetic ROM
  expect(lsdjAssetCatalog.baseSlots(risaRomFull(), "palette")).toEqual([]); // not an LSDj image
});

test("risaAssetCatalog.baseSlots parses the base ROM's themes/fonts/kits (empty for a non-risa image)", () => {
  const rom = risaRomFull();
  const themes = risaAssetCatalog.baseSlots(rom, "theme");
  expect(themes.length).toBe(16);
  expect(themes[0]).toEqual({ slot: 0, name: "TH0" });
  const fonts = risaAssetCatalog.baseSlots(rom, "font");
  expect(fonts.length).toBe(4);
  expect(fonts[0]).toEqual({ slot: 0, name: "Font 0" }); // CHR has no name table → synthesized
  expect(risaAssetCatalog.baseSlots(rom, "kit")).toEqual([{ slot: 0, name: "TEST" }]); // only the populated slot
  expect(risaAssetCatalog.baseSlots(gbRomBattery(), "theme")).toEqual([]); // not a risa image
});
