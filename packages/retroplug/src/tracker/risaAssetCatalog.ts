// The risa implementation of AssetCatalog — the base-ROM asset parse (relocated verbatim from the menu's old
// risaInventory). No overrides here (that's the role config, read live at the menu) and no memoization (the UI
// caches the file read). The file-dialog Export/Replace stay in the menu (they own the .rit/.chr/.rkit formats).
import type { AssetCatalog, AssetSlot, AssetOverride } from "./assetCatalog";
import type { ConstructCaps } from "../systemRoles";
import { RisaRom, KIT_BANK_COUNT } from "../risa/rom";
import { applyOverridesToRom, type RisaAssetOverride } from "../risaAssetsRole";

export const risaAssetCatalog: AssetCatalog = {
  assetRole: "risa-assets",
  types: [
    { kind: "theme", title: "Themes", noun: "Theme", patterns: ["*.rit"], ext: ".rit", addable: false, maxSlots: 0 },
    { kind: "font", title: "Fonts", noun: "Font", patterns: ["*.chr"], ext: ".chr", addable: false, maxSlots: 0 },
    { kind: "kit", title: "Kits", noun: "Kit", patterns: ["*.rkit"], ext: ".rkit", addable: true, maxSlots: KIT_BANK_COUNT },
  ],
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[] {
    const rom = RisaRom.fromBytes(romBytes);
    if (!rom.isRisa) return [];
    if (kind === "theme") return rom.themes().map((t) => ({ slot: t.slot, name: t.theme.name.trim() || `Theme ${t.slot}` }));
    if (kind === "font") return rom.fonts().map((f) => ({ slot: f.slot, name: `Font ${f.slot}` }));
    if (kind === "kit") return rom.kits().map((k) => ({ slot: k.slot, name: k.name || `Kit ${k.slot}` }));
    return [];
  },
  // The role's own patcher. The loose AssetOverride entries ARE the typed ones (the menu builds them; the
  // extra fields ride through structurally).
  applyOverrides(romBytes: Uint8Array, overrides: AssetOverride[], caps: ConstructCaps, onSkip): Uint8Array {
    return applyOverridesToRom(romBytes, overrides as RisaAssetOverride[], caps, onSkip);
  },
};
