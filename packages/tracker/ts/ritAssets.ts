// What the `risa-assets` and `bliptoaster-assets` roles have in common, which turned out to be nearly all
// of it: the override shape, its zod schema, and the patcher that folds one override onto an open ROM. The roles
// themselves are the carts' (they name their own ROM view); this is the shared "rit" asset contract.
//
// The two carts expose the same asset surface - a 7-role theme table, 8 KB CHR font banks, 8 KB DMC kit
// banks - so the two roles had grown ~120 verbatim lines apart from the class name and one constant that
// both spell 0x2000. BlipToaster's override interface was already declared with `theme?: RisaTheme`,
// which is the clearest statement that these were one type wearing two names.
//
// LSDj is not here. Its assets are kits, palettes and font IMAGES, its overrides carry colorSets rather
// than a theme, and it validates by trial-applying to a parsed ROM rather than by length. Sharing a name
// with it would cost more than it saved.
import type { Host } from "@retroplug/core/host";
import type { RisaTheme } from "./nes/types";
import { z } from "zod";
import { CHR_BANK_SIZE, KIT_BANK_SIZE } from "./nes/constants";
import { encodeThemeRecord, encodeThemeName, normalizeTheme } from "./nes/theme";
import { isBankPopulated } from "./nes/kitBank";

// One font slot = one 8 KB CHR bank, on both carts. Re-exported rather than redeclared: ./nes/constants already owns
// it, and both carts' asset roles import it from here.
export { CHR_BANK_SIZE };

/** One asset override for `slot`. THEMES are palette indices, stored INLINE as a readable `theme` object
 *  (name + 7 "0xNN" role indices — no file, no base64). FONTS and KITS are binary, so they LINK their file
 *  on disk by `path` (a `.chr` bank / a pre-built 8 KB `.rkit` DMC bank, read at construct). A kit override
 *  with `erase: true` empties the slot instead (a "delete this kit" override). `name` is a display label. */
export interface RitAssetOverride {
  type: "theme" | "font" | "kit";
  slot: number;
  name?: string;
  theme?: RisaTheme; // theme — stored inline (7 palette-index roles, no file)
  path?: string; // font / kit — the .chr / .rkit bank file on disk
  erase?: boolean; // kit — empty the slot instead of linking a bank
}

/** The override list a `*-assets` role config carries, typed for the `.rit` actions. One raw cast lives here, so the
 *  shared actions and both carts' role modules read the same list the same way (the config is raw JSON from a saved
 *  project, and each entry's shape is validated where it is APPLIED rather than here). */
export function readRitOverrides(config: Record<string, unknown> | undefined): RitAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as RitAssetOverride[]) : [];
}

export const ritThemeSchema = z.object({
  name: z.string(),
  bg: z.string(),
  normal: z.string(),
  shaded: z.string(),
  alternate: z.string(),
  status: z.string(),
  cursor: z.string(),
  selection: z.string(),
});

export const ritOverrideSchema = z.object({
  type: z.enum(["theme", "font", "kit"]),
  slot: z.number().int().nonnegative(),
  name: z.string().optional(),
  theme: ritThemeSchema.optional(),
  path: z.string().optional(),
  erase: z.boolean().optional(),
});

/** The slice of a parsed ROM the patcher touches. RisaRom and BlipToasterRom both satisfy it as written. */
export interface RitAssetRom {
  setTheme(slot: number, recordBytes: Uint8Array, nameBytes: Uint8Array): void;
  setChrFontSlot(slot: number, bytes: Uint8Array): void;
  setKit(slot: number, bank: Uint8Array): void;
  clearKitBank(slot: number): void;
}

/** Apply one override onto an open ROM. Themes apply from their inline object; fonts and kits read their
 *  linked file. Isolated + THROWING so the caller can try/catch per entry - a moved file or a bad asset
 *  skips that override rather than failing the whole construct. */
export function applyRitOverride(rom: RitAssetRom, ov: RitAssetOverride, host: Host<"files">): void {
  if (ov.type === "theme") {
    if (!ov.theme) throw new Error(`theme override slot ${ov.slot}: no theme`);
    const theme = normalizeTheme(ov.theme);
    rom.setTheme(ov.slot, encodeThemeRecord(theme), encodeThemeName(theme));
    return;
  }
  if (ov.type === "kit") {
    if (ov.erase) {
      rom.clearKitBank(ov.slot); // "delete this kit" override
      return;
    }
    if (!ov.path) throw new Error(`kit override slot ${ov.slot}: no path`);
    const bank = host.files.readFile(ov.path);
    if (!bank) throw new Error(`kit override slot ${ov.slot}: cannot read ${ov.path}`);
    if (bank.length !== KIT_BANK_SIZE) throw new Error(`kit override slot ${ov.slot}: .rkit must be exactly 8 KB`);
    if (!isBankPopulated(bank)) throw new Error(`kit override slot ${ov.slot}: not a populated kit bank`);
    rom.setKit(ov.slot, bank); // the cart's own view of a kit write; risa's keeps its metadata mirror in step
    return;
  }
  // font
  if (!ov.path) throw new Error(`font override slot ${ov.slot}: no path`);
  const bytes = host.files.readFile(ov.path);
  if (!bytes) throw new Error(`font override slot ${ov.slot}: cannot read ${ov.path}`);
  if (bytes.length !== CHR_BANK_SIZE) throw new Error(`font override slot ${ov.slot}: .chr must be exactly 8 KB`);
  rom.setChrFontSlot(ov.slot, bytes);
}
