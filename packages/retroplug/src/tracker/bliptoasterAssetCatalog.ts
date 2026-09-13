// The BlipToaster implementation of AssetCatalog — the base-ROM asset parse over BlipToasterRom, the BlipToaster twin
// of ./risaAssetCatalog.ts. Every type is a FIXED list the cart bakes and switches between live over MIDI: 16
// themes (CC 16), 4 CHR fonts (CC 17), and 16 DMC kits (ch5 CC 14) on a banking build
// (VRC6/VRC7/S5B/FME-7/N163) or one fixed kit on NROM. The counts are READ from the ROM, not assumed — which is
// why the kit type is ROM-aware (resolveTypes): Replace-only on NROM, addable/16 on a banking cart. Themes and
// fonts are never addable: both tables are fixed-size, so a replace overwrites an entry and can never grow the
// list. The file-dialog Export/Replace stay in the menu (they own the .rit/.rkit/.chr formats).
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
