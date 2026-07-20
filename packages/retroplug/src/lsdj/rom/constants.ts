// LSDj Game Boy ROM (.gb) layout constants — the static ASSET structures baked into the ROM image
// (sample kits, fonts, palettes), as opposed to the .sav song data (../codec) or live WRAM
// (../runtime). Ported from the old C++ ecs `src/lsdj/Rom.h`, cross-checked against real ROMs
// (offsets verified in lsdj9_4_2.gb). All multi-byte values are little-endian (GB native).

export const ROM_SIZE = 0x100000; // 1 MiB
export const BANK_SIZE = 0x4000; // 16 KiB per bank; 64 banks total

// --- Kits (sample banks) --------------------------------------------------------------------------
// Each kit occupies one whole bank. Kits live in the banks listed by KIT_LOOKUP (51 slots); the gaps
// (banks 0-7, 27-31) hold the LSDj program, fonts, palettes and graphics.
export const KIT_COUNT = 51;
export const KIT_LOOKUP: readonly number[] = [
  8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
  51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
];

export const KIT_MAX_SAMPLES = 15;
export const KIT_MAX_SAMPLE_SPACE = 0x3fa0; // bytes of sample data a kit can hold
export const KIT_NAME_OFFSET = 0x52; // 6-byte kit name (in-bank)
export const KIT_NAME_SIZE = 6;
export const KIT_SAMPLE_NAME_OFFSET = 0x22; // 15 × 3-byte sample names (in-bank)
export const KIT_SAMPLE_NAME_SIZE = 3;
export const KIT_SAMPLE_DATA_OFFSET = BANK_SIZE - KIT_MAX_SAMPLE_SPACE; // 0x60: first sample byte in-bank

// The 16-entry u16le offset table at the bank start. Entry 0 is the sample-data start as a GB address
// (0x4060 = 0x4000 + KIT_SAMPLE_DATA_OFFSET) and doubles as the "valid kit" magic; entries 1.. are each
// sample's END address; a 0x0000 entry terminates the list. An empty kit reads 0xFFFF at entry 0.
export const KIT_OFFSET_TABLE = 0x00;
export const KIT_OFFSET_ENTRIES = 16;
export const KIT_VALID_MAGIC = 0x4000 + KIT_SAMPLE_DATA_OFFSET; // 0x4060 (first offset entry, LE 60 40)
export const KIT_EMPTY_MAGIC = 0xffff; // first offset entry of an empty kit

// Sample nibble encoding (the GB wave format LSDj packs kit audio into; see native SampleUtil.hpp):
//  - stored 4-bit value is INVERTED: stored = 0xF - amplitude (decode: amp = 0xF - stored).
//  - LSDj 9.2.0+ ROTATES each 32-sample frame right by one (encoded position i holds source sample i-1,
//    position 0 the last) to dodge a GB wave-refresh bug. Older builds store un-rotated.
export const GB_SAMPLE_RATE = 11468; // the Game Boy kit playback rate everything resamples to
export const SAMPLES_PER_FRAME = 32; // one GB wave frame
export const BYTES_PER_FRAME = 16; // 32 × 4-bit
// Rotation applies from this LSDj version up (major.minor). 9.2 → true for our 9.x ROMs.
export const ROTATION_MIN_MAJOR = 9;
export const ROTATION_MIN_MINOR = 2;

// --- Palettes -------------------------------------------------------------------------------------
// Located by scanning bank 1 for PALETTE_CHECK; the block base is `count` × 40 bytes before it, where the
// count is version-derived (names.paletteCount — 9.4.2 has 7). Each palette is 40 bytes = 5 colour-sets ×
// 8 bytes (a GBC 4-colour set each). PALETTE_COUNT is only the FALLBACK when the name landmark is absent.
export const PALETTE_COUNT = 6;
export const PALETTE_SIZE = 40;
export const PALETTE_NAME_SIZE = 5; // name slot: 4 chars + NUL
export const PALETTE_COLOR_SET_COUNT = 5;
export const PALETTE_COLOR_SET_SIZE = 8;

// --- Fonts ----------------------------------------------------------------------------------------
// Located by scanning bank 30 for FONT_HEADER_CHECK (the fixed header). 3 fonts, each 0xE96 bytes: a
// 130-byte header then 71 tiles of 8×8 pixels in the standard GB 2bpp format (16 bytes/tile). Names live
// in the bank-27 table (names.ts), NOT the header.
export const FONT_COUNT = 3;
export const FONT_SIZE = 0xe96;
export const FONT_HEADER_SIZE = 130;
export const FONT_NAME_SIZE = 4;
export const FONT_TILE_COUNT = 71;
export const FONT_TILE_SIZE = 16; // 8×8 @ 2bpp
export const FONT_TILE_WIDTH = 8;
export const FONT_TILE_HEIGHT = 8;
export const FONT_TILES_X = 8; // font image is 8 tiles (64px) wide

// The 46 "graphics" glyphs (extended font, e.g. BLSD.png) live in ONE block SHARED by all 3 fonts, sitting
// immediately BEFORE the font block: gfxBase = fontBase − FONT_GFX_TILE_COUNT × FONT_TILE_SIZE (lsdpatch
// RomUtilities.findGfxFontOffset / findFontOffset). A 64×72 PNG carries the 71 main tiles; a 64×120 PNG
// also carries these 46. Importing the extended set updates the graphics for every font.
export const FONT_GFX_TILE_COUNT = 46;
export const FONT_GFX_BLOCK_SIZE = FONT_GFX_TILE_COUNT * FONT_TILE_SIZE; // 0x2E0

// Within a font's 0xE96 block, tiles 2..70 keep an inverted copy at +FONT_VARIANT_STRIDE and a shaded copy
// at +2×FONT_VARIANT_STRIDE (lsdpatch generateShadedAndInvertedTiles) — regenerated after any main-tile
// edit so LSDj's inverted/shaded UI contexts render correctly.
export const FONT_VARIANT_STRIDE = 0x4d2;

// Font PNG greys ↔ 2bpp shade (lsdpatch grayIndexToColor + loadImageData). Export: value → RGB. Import:
// bucket by luminance max(r,g,b) — ≥192 → 0 (white), ≥64 → 1 (grey), else → 3 (black); value 2 is unused
// by LSDj fonts (import never produces it), but decodes to 0x808080 for completeness.
export const FONT_SHADE_RGB: readonly number[] = [0xffffff, 0x969696, 0x808080, 0x000000];
export const FONT_LUM_WHITE = 192; // ≥ → shade 0
export const FONT_LUM_GREY = 64; //  ≥ → shade 1 (else shade 3)

// --- Names (bank 27) -----------------------------------------------------------------------------
// Font + palette names + the palette COUNT all anchor on the "grayscale palette names" landmark in bank
// 27: a run of three 5-byte name slots (4 non-zero chars + a NUL). Ported from lsdpatch RomUtilities.
export const NAME_BANK = 27;

// --- Section-location byte patterns (the TS port of Rom::findOffset markers) ----------------------
export const VERSION_CHECK: readonly number[] = [0x4c, 0x53, 0x44, 0x6a, 0x2d, 0x76]; // "LSDj-v" (@0x134)
// 17 zero bytes then 0x48 0x48 0x48 — the head of the first palette's colour data (bank 1).
export const PALETTE_CHECK: readonly number[] = [
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x48, 0x48, 0x48,
];
// The font block's fixed header start (bank 30): a stable LSDj character-map (`01 47` then a long run of
// `02`), identical across all 3 fonts AND across stock vs custom-font ROMs — so it anchors the font block
// even when the user's custom glyphs have overwritten the tiles. (The old glyph-tile marker broke on
// custom fonts.) The match lands ON the font base; getFont indexes from there.
export const FONT_HEADER_CHECK: readonly number[] = [
  0x01, 0x47, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
];
