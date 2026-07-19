// risa (NES/MMC5) ROM layout + asset constants, ported from risa's own tools/rom_patcher/src
// (rom.js + theme/constants.js + theme/palette.js + font_editor/constants.js). Offsets are DERIVED from
// the iNES header at runtime (see rom.ts computeLayout) — nothing here hard-codes a per-ROM address.

export const HEADER_SIZE = 0x10;
export const PRG_16K_SIZE = 0x4000;
export const PRG_8K_SIZE = 0x2000;
export const PRG_FIXED_SIZE = 0x2000; // the fixed bank (theme table lives here)
export const KIT_BANK_COUNT = 32; // the 32 DPCM kit banks at the top of PRG (kits are deferred to M5)

export const CHR_BANK_SIZE = 0x2000; // one font slot = one 8 KB CHR bank
export const FONT_BANK_COUNT = 4;
export const TILE_COUNT = 512;
export const TILE_BYTES = 16;

// --- Themes (NES palette) ---
export const THEME_VERSION = 1;
export const THEME_COUNT = 16;
export const THEME_NAME_SIZE = 4;
export const THEME_RECORD_SIZE = 7;
export const THEME_META_MAGIC = [0xa5, 0x5a, 0x54, 0x48, 0x4d, 0x45]; // "·ZTHME"
// Role order is LOAD-BEARING — each theme record is 7 bytes, one 6-bit NES palette index per role.
export const ROLES = ["bg", "normal", "shaded", "alternate", "status", "cursor", "selection"] as const;
export type ThemeRole = (typeof ROLES)[number];

// The 64-entry NES master palette (compiled into the tool, not read from ROM); index 0x00..0x3F -> #rrggbb.
export const NES_PALETTE: string[] = [
  "#656764", "#002b89", "#1512a7", "#3901a0", "#5c007d", "#71003a", "#6d0504", "#5f1900",
  "#323500", "#0b4900", "#005200", "#004f08", "#00404c", "#000000", "#000000", "#000000",
  "#b8b8b8", "#155fd8", "#4140fe", "#7627fe", "#a019cb", "#b71d7b", "#b13422", "#9a4e00",
  "#6b6d00", "#368800", "#0c9404", "#008f33", "#027b8e", "#000000", "#000000", "#000000",
  "#fefefe", "#63b1fc", "#9290ff", "#c776ff", "#fb71c7", "#fe6ecc", "#fe816f", "#ea9f22",
  "#bcbf00", "#88d700", "#5de430", "#45e082", "#48cdde", "#4f4f4f", "#000000", "#000000",
  "#fefefe", "#c0dffe", "#d3d2fe", "#e9c8ff", "#fcc2ff", "#fec4ea", "#feccc5", "#f7d7a4",
  "#e2e594", "#d1ed98", "#bcf4ab", "#b4f2cb", "#b3ecf3", "#b8b8b8", "#000000", "#000000",
];
