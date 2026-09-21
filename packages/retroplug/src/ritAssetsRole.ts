// What the `risa-assets` and `bliptoaster-assets` roles have in common, which turned out to be nearly all
// of it: the override shape, its zod schema, and the patcher that folds one override onto an open ROM.
//
// The two carts expose the same asset surface - a 7-role theme table, 8 KB CHR font banks, 8 KB DMC kit
// banks - so the two roles had grown ~120 verbatim lines apart from the class name and one constant that
// both spell 0x2000. BlipToaster's override interface was already declared with `theme?: RisaTheme`,
// which is the clearest statement that these were one type wearing two names.
//
// LSDj is not here. Its assets are kits, palettes and font IMAGES, its overrides carry colorSets rather
// than a theme, and it validates by trial-applying to a parsed ROM rather than by length. Sharing a name
// with it would cost more than it saved.
import type { ConstructCaps } from "./systemRoles";
import type { RisaTheme } from "./risa/rom";
import { z } from "./configSchema";
import { encodeThemeRecord, encodeThemeName, normalizeTheme, KIT_BANK_SIZE, CHR_BANK_SIZE, isBankPopulated } from "./risa/rom";

// One font slot = one 8 KB CHR bank, on both carts. Re-exported rather than redeclared: risa/rom already
// owns it, and BlipToaster's role was already importing it from there.
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
export function applyRitOverride(rom: RitAssetRom, ov: RitAssetOverride, caps: ConstructCaps): void {
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
    const bank = caps.readFile(ov.path);
    if (!bank) throw new Error(`kit override slot ${ov.slot}: cannot read ${ov.path}`);
    if (bank.length !== KIT_BANK_SIZE) throw new Error(`kit override slot ${ov.slot}: .rkit must be exactly 8 KB`);
    if (!isBankPopulated(bank)) throw new Error(`kit override slot ${ov.slot}: not a populated kit bank`);
    rom.setKit(ov.slot, bank); // dual-writes the resident metadata mirror
    return;
  }
  // font
  if (!ov.path) throw new Error(`font override slot ${ov.slot}: no path`);
  const bytes = caps.readFile(ov.path);
  if (!bytes) throw new Error(`font override slot ${ov.slot}: cannot read ${ov.path}`);
  if (bytes.length !== CHR_BANK_SIZE) throw new Error(`font override slot ${ov.slot}: .chr must be exactly 8 KB`);
  rom.setChrFontSlot(ov.slot, bytes);
}
