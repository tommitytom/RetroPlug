// The iNES asset formats the NES tracker carts share: the header and PRG bank sizes, the 8 KB DMC kit bank, the CHR
// font bank and its 16x16 tile grid, the 7-role theme record and the 64-entry NES master palette, and the PAL DPCM
// playback rates. Ported from risa's own tools/rom_patcher/src (rom.js + theme/constants.js + theme/palette.js +
// font_editor/constants.js), and shared verbatim with BlipToaster, whose ROM bakes the same banks, fonts and theme
// records (its own kit-metadata mirror and theme-table STRIDE are its own, in its package).
//
// These are FORMAT numbers, not one ROM's layout: offsets are DERIVED from the iNES header at runtime by whoever reads
// a ROM (risa's rom.ts computeLayout, BlipToaster's rom.ts), and a cart's own bank counts and mirror sizes stay with
// that cart. Anything here that changed would change what a stored ROM patch means.

export const HEADER_SIZE = 0x10;
export const PRG_16K_SIZE = 0x4000;
export const PRG_8K_SIZE = 0x2000;

// --- Kit bank (DPCM) - tools/rom_patcher/src/kit_editor/constants.js ---
export const KIT_BANK_SIZE = 0x2000; // one kit bank = 8 KB
export const KIT_SAMPLE_REGION = 0x1ec0; // 7872 B of packed DPCM at the bank head
export const KIT_NAME_OFFSET = 0x1ec0;
export const KIT_NAME_SIZE = 16;
export const KIT_NAME_MAX_CHARS = 6; // kit name is uppercased A-Z/0-9/'-', kept to <=6 chars then NUL-padded
export const KIT_SAMPLE_NAMES = 0x1ed0; // 16 x 3-char sample names
export const KIT_SAMPLE_NAME_LEN = 3;
export const KIT_INDEX_OFFSET = 0x1f00; // 16 x 4-byte index entries [addr, lenReg, rate, flags]
export const KIT_INDEX_ENTRY = 4;
export const KIT_SLOT_COUNT = 16;
export const KIT_MAGIC = 0xa5; // "populated" marker
export const KIT_MAGIC_OFFSET = 0x1f40;
export const KIT_SLOT_EMPTY = 0xff;
export const KIT_FLAG_LOOP = 0x01;
export const SAMPLE_ALIGN = 64; // DPCM start-address granularity (addr reg = offset / 64)
export const LENGTH_STEP = 16; // byte length = lenReg * 16 + 1

// PAL DPCM playback rates (Hz), index 0..15 (wav2dmc.py PAL_DPCM_RATES_HZ). Default index 12.
export const PAL_DPCM_RATES_HZ = [
  4177.4, 4696.63, 5261.41, 5579.22, 6023.94, 7044.94, 7917.18, 8397.01,
  9446.63, 11233.8, 12595.5, 14089.89, 16965.4, 21315.52, 25191.0, 33252.09,
];

export const CHR_BANK_SIZE = 0x2000; // one font slot = one 8 KB CHR bank
export const TILE_COUNT = 512;
export const TILE_BYTES = 16;

// --- Themes (NES palette) ---
export const THEME_VERSION = 1;
export const THEME_COUNT = 16;
export const THEME_NAME_SIZE = 4;
export const THEME_RECORD_SIZE = 7;
export const THEME_META_MAGIC = [0xa5, 0x5a, 0x54, 0x48, 0x4d, 0x45]; // "·ZTHME"
// Role order is LOAD-BEARING - each theme record is 7 bytes, one 6-bit NES palette index per role.
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
