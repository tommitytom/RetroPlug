// Plain typed shapes for extracted LSDj ROM assets (kits / palettes / fonts). Like the runtime module,
// these are plain interfaces — a ROM can't be round-tripped from a model (it's mostly opaque program
// code), so there is no zod SSOT here; the LsdjRom view reads these out and patches bytes in place.

/** An 8-bit-per-channel colour, expanded from the ROM's packed GBC 15-bit RGB555. */
export interface RomColor {
  r: number;
  g: number;
  b: number;
}

/** One palette colour-set = a full GBC 4-colour palette (8 bytes). LSDj treats colours 0 and 3 as the
 *  editable pair and blends 1 and 2 between them, but all four are stored raw and exposed here. */
export interface RomColorSet {
  colors: RomColor[]; // 4 colours
}

/** A decoded palette: its slot, name (≤4 chars, from the bank-27 name table) and 5 colour-sets. */
export interface RomPalette {
  index: number;
  name: string;
  colorSets: RomColorSet[]; // 5 sets
}

/** One extracted kit sample: its 3-char name and mono PCM in [-1, 1] (from the ROM's 4-bit nibbles). */
export interface RomSample {
  name: string;
  pcm: Float32Array;
}

/** A decoded kit (one bank): its slot, validity, 6-char name, and populated samples (≤ 15). */
export interface RomKit {
  index: number; // 0..KIT_COUNT-1 (position in KIT_LOOKUP)
  bank: number; // the ROM bank this kit occupies
  valid: boolean; // starts with the 0x4060 magic
  empty: boolean; // starts with 0xFFFF
  name: string;
  samples: RomSample[];
}

/** One 8×8 font tile: 64 pixels row-major, each a 2bpp index 0..3. */
export type RomFontTile = number[];

/** A decoded font: its slot, name (≤4 chars, from the bank-27 name table) and 71 tiles (the main glyph
 *  set; the alternate/graphics copy within the 0xE96 block is not modelled). */
export interface RomFont {
  index: number;
  name: string;
  tiles: RomFontTile[];
}

/** A raw RGBA8888 image (`rgba` is `width*height*4` bytes, row-major, top-to-bottom) — the intermediate
 *  form for font PNG import/export. PNG bytes ↔ this happens at the caller (backend.pngEncode/pngDecode);
 *  this module maps it to/from font tiles, so the same code serves the CLI and the plugin UI. */
export interface RomImage {
  width: number;
  height: number;
  rgba: Uint8Array;
}
