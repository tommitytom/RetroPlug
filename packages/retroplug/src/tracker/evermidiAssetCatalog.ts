// The EverMIDI implementation of AssetCatalog — the base-ROM asset parse over EverMidiRom, the EverMIDI twin
// of ./risaAssetCatalog.ts. EverMIDI (NROM) bakes one theme + one DMC kit + one CHR font slot. It's
// Replace-only (a single slot per type), so no type is `addable`. The file-dialog Export/Replace stay in the
// menu (they own the .rit/.rkit/.chr formats).
import type { AssetCatalog, AssetSlot } from "./assetCatalog";
import { EverMidiRom } from "../evermidi/rom";

export const evermidiAssetCatalog: AssetCatalog = {
  assetRole: "evermidi-assets",
  types: [
    { kind: "theme", title: "Themes", noun: "Theme", patterns: ["*.rit"], ext: ".rit", addable: false, maxSlots: 0 },
    { kind: "kit", title: "Kits", noun: "Kit", patterns: ["*.rkit"], ext: ".rkit", addable: false, maxSlots: 0 },
    { kind: "font", title: "Fonts", noun: "Font", patterns: ["*.chr"], ext: ".chr", addable: false, maxSlots: 0 },
  ],
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[] {
    const rom = EverMidiRom.fromBytes(romBytes);
    if (!rom.isEverMidi) return [];
    if (kind === "theme") return rom.themes().map((t) => ({ slot: t.slot, name: t.theme.name.trim() || `Theme ${t.slot}` }));
    if (kind === "kit") return rom.kits().map((k) => ({ slot: k.slot, name: k.name || `Kit ${k.slot}` }));
    if (kind === "font") return rom.fonts().map((f) => ({ slot: f.slot, name: `Font ${f.slot}` }));
    return [];
  },
};
