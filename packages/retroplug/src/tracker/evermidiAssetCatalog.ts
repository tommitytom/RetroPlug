// The EverMIDI implementation of AssetCatalog — the base-ROM asset parse over EverMidiRom, the EverMIDI twin
// of ./risaAssetCatalog.ts. EverMIDI (NROM) bakes one DMC kit + one CHR font slot (no themes yet — that
// needs a ROM-side retrofit). It's Replace-only (a single kit / font slot), so neither type is `addable`.
// The file-dialog Export/Replace stay in the menu (they own the .rkit/.chr formats).
import type { AssetCatalog, AssetSlot } from "./assetCatalog";
import { EverMidiRom } from "../evermidi/rom";

export const evermidiAssetCatalog: AssetCatalog = {
  assetRole: "evermidi-assets",
  types: [
    { kind: "kit", title: "Kits", noun: "Kit", patterns: ["*.rkit"], ext: ".rkit", addable: false, maxSlots: 0 },
    { kind: "font", title: "Fonts", noun: "Font", patterns: ["*.chr"], ext: ".chr", addable: false, maxSlots: 0 },
  ],
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[] {
    const rom = EverMidiRom.fromBytes(romBytes);
    if (!rom.isEverMidi) return [];
    if (kind === "kit") return rom.kits().map((k) => ({ slot: k.slot, name: k.name || `Kit ${k.slot}` }));
    if (kind === "font") return rom.fonts().map((f) => ({ slot: f.slot, name: `Font ${f.slot}` }));
    return [];
  },
};
