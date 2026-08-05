// The LSDj implementation of AssetCatalog — the base-ROM asset parse (relocated verbatim from the menu's old
// lsdjInventory). No overrides here (that's the role config, read live at the menu) and no memoization (the UI
// caches the file read). The file-dialog Export/Replace stay in the menu (they own the .kit/.lsdpal/.png formats).
import type { AssetCatalog, AssetSlot, AssetOverride } from "./assetCatalog";
import type { ConstructCaps } from "../systemRoles";
import { LsdjRom, KIT_COUNT } from "../lsdj/rom";
import { applyOverridesToRom, type LsdjAssetOverride } from "../lsdjAssetsRole";

export const lsdjAssetCatalog: AssetCatalog = {
  assetRole: "lsdj-assets",
  types: [
    { kind: "kit", title: "Kits", noun: "Kit", patterns: ["*.kit"], ext: ".kit", addable: true, maxSlots: KIT_COUNT },
    { kind: "font", title: "Fonts", noun: "Font", patterns: ["*.png"], ext: ".png", addable: false, maxSlots: 0 },
    { kind: "palette", title: "Palettes", noun: "Palette", patterns: ["*.lsdpal"], ext: ".lsdpal", addable: false, maxSlots: 0 },
  ],
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[] {
    const rom = LsdjRom.fromBytes(romBytes);
    if (!rom.isLsdj) return [];
    if (kind === "kit") return rom.kits().filter((k) => k.valid).map((k) => ({ slot: k.index, name: k.name() || `Kit ${k.index}` }));
    if (kind === "palette") return rom.palettes().map((p) => ({ slot: p.index, name: p.name || `Palette ${p.index}` }));
    if (kind === "font") return rom.fonts().map((f) => ({ slot: f.index, name: f.name || `Font ${f.index}` }));
    return [];
  },
  // The role's own patcher. The loose AssetOverride entries ARE the typed ones (the menu builds them; the
  // extra fields ride through structurally).
  applyOverrides(romBytes: Uint8Array, overrides: AssetOverride[], caps: ConstructCaps, onSkip): Uint8Array {
    return applyOverridesToRom(romBytes, overrides as LsdjAssetOverride[], caps, onSkip);
  },
};
