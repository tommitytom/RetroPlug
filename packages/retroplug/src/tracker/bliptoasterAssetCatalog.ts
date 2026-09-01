// The BlipToaster implementation of AssetCatalog — the base-ROM asset parse over BlipToasterRom, the BlipToaster twin
// of ./risaAssetCatalog.ts. BlipToaster bakes one theme + one CHR font slot; the DMC kit is single-slot on NROM
// but up to 16 SWITCHABLE banks on a banking build (VRC6/VRC7/S5B/FME-7/N163). So the kit type is ROM-aware
// (resolveTypes): Replace-only on NROM, addable/16 on a banking cart. Themes/fonts stay single-slot.
// The file-dialog Export/Replace stay in the menu (they own the .rit/.rkit/.chr formats).
import type { AssetCatalog, AssetSlot, AssetTypeInfo, AssetOverride } from "./assetCatalog";
import type { ConstructCaps } from "../systemRoles";
import { BlipToasterRom } from "../bliptoaster/rom";
import { applyOverridesToRom, type BlipToasterAssetOverride } from "../bliptoasterAssetsRole";

const THEME_TYPE: AssetTypeInfo = { kind: "theme", title: "Themes", noun: "Theme", patterns: ["*.rit"], ext: ".rit", addable: false, maxSlots: 0 };
const FONT_TYPE: AssetTypeInfo = { kind: "font", title: "Fonts", noun: "Font", patterns: ["*.chr"], ext: ".chr", addable: false, maxSlots: 0 };
const kitType = (capacity: number): AssetTypeInfo => ({
  kind: "kit",
  title: "Kits",
  noun: "Kit",
  patterns: ["*.rkit"],
  ext: ".rkit",
  addable: capacity > 1, // NROM (capacity 1) is Replace-only; a banking cart gets Add.../Delete
  maxSlots: capacity,
});

export const bliptoasterAssetCatalog: AssetCatalog = {
  assetRole: "bliptoaster-assets",
  // Default (ROM-independent) shape: single-kit. resolveTypes refines the kit type per cart below.
  types: [THEME_TYPE, kitType(1), FONT_TYPE],
  resolveTypes(romBytes: Uint8Array): AssetTypeInfo[] {
    const rom = BlipToasterRom.fromBytes(romBytes);
    return [THEME_TYPE, kitType(rom.isBlipToaster ? rom.kitBankCapacity() : 1), FONT_TYPE];
  },
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[] {
    const rom = BlipToasterRom.fromBytes(romBytes);
    if (!rom.isBlipToaster) return [];
    if (kind === "theme") return rom.themes().map((t) => ({ slot: t.slot, name: t.theme.name.trim() || `Theme ${t.slot}` }));
    if (kind === "kit") return rom.kits().map((k) => ({ slot: k.slot, name: k.name || `Kit ${k.slot}` }));
    if (kind === "font") return rom.fonts().map((f) => ({ slot: f.slot, name: `Font ${f.slot}` }));
    return [];
  },
  applyOverrides(romBytes: Uint8Array, overrides: AssetOverride[], caps: ConstructCaps, onSkip): Uint8Array {
    return applyOverridesToRom(romBytes, overrides as BlipToasterAssetOverride[], caps, onSkip);
  },
};
