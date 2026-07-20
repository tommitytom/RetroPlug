// LSDj fonts. The font block is located by scanning bank 30 for FONT_HEADER_CHECK — the fixed header at
// each font's start (stable even when custom glyphs overwrote the tiles; the old glyph-tile marker broke
// on custom fonts). Each of the 3 fonts is 0xE96 bytes; its main glyph set is 71 tiles starting after a
// 130-byte header. Tiles are the standard Game Boy 2bpp format: 16 bytes = 8 rows × 2 bytes, pixel =
// (plane0 bit) | (plane1 bit << 1), giving a 0..3 shade per pixel. Font NAMES live in a separate bank-27
// table (see names.ts) and are supplied to FontView.
import { BitView, BitWriter } from "../codec/bits";
import { findPattern } from "./find";
import type { RomFont, RomFontTile } from "./types";
import {
  FONT_GFX_BLOCK_SIZE,
  FONT_GFX_TILE_COUNT,
  FONT_HEADER_CHECK,
  FONT_HEADER_SIZE,
  FONT_SIZE,
  FONT_TILE_COUNT,
  FONT_TILE_SIZE,
  FONT_VARIANT_STRIDE,
} from "./constants";

const FONT_BANK = 30;

/** The absolute ROM offset of the font block (font 0's header start), or -1 if not located. */
export function findFontBase(rom: Uint8Array): number {
  return findPattern(rom, FONT_BANK, FONT_HEADER_CHECK, 0);
}

export class FontView {
  private readonly view: BitView;
  private readonly writer: BitWriter;
  private readonly tilesBase: number;
  // The shared 46-tile graphics block sits immediately before the font block (base − FONT_GFX_BLOCK_SIZE),
  // so it's the same for every font index.
  private readonly gfxBase: number;

  /** `base` = findFontBase(rom); `name` from names.fontName(). Construct via LsdjRom.fonts(). */
  constructor(rom: Uint8Array, base: number, readonly index: number, readonly name = "") {
    this.view = new BitView(rom);
    this.writer = new BitWriter(rom);
    this.tilesBase = base + index * FONT_SIZE + FONT_HEADER_SIZE;
    this.gfxBase = base - FONT_GFX_BLOCK_SIZE;
  }

  private tileOffset(tile: number): number {
    return this.tilesBase + tile * FONT_TILE_SIZE;
  }

  // Read a 2bpp 8×8 tile at an absolute ROM offset → 64 row-major pixel indices (0..3).
  private readTileAt(off: number): RomFontTile {
    const px: number[] = [];
    for (let y = 0; y < 8; y++) {
      const p0 = this.view.u8(off + y * 2);
      const p1 = this.view.u8(off + y * 2 + 1);
      for (let x = 0; x < 8; x++) {
        const bit = 7 - x;
        px.push(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1));
      }
    }
    return px;
  }

  // Write 64 pixel indices (0..3) as a 2bpp 8×8 tile at an absolute ROM offset.
  private writeTileAt(off: number, px: readonly number[]): void {
    for (let y = 0; y < 8; y++) {
      let p0 = 0;
      let p1 = 0;
      for (let x = 0; x < 8; x++) {
        const v = px[y * 8 + x] & 0x3;
        const bit = 7 - x;
        p0 |= (v & 1) << bit;
        p1 |= ((v >> 1) & 1) << bit;
      }
      this.writer.setU8(off + y * 2, p0);
      this.writer.setU8(off + y * 2 + 1, p1);
    }
  }

  /** Tile `tile` (0..70) as 64 row-major 2bpp pixel indices (0..3). */
  tile(tile: number): RomFontTile {
    return this.readTileAt(this.tileOffset(tile));
  }

  /** Patch tile `tile` (0..70) in place from 64 row-major 2bpp pixel indices (0..3). */
  setTile(tile: number, px: readonly number[]): void {
    if (tile < 0 || tile >= FONT_TILE_COUNT || px.length < 64) return;
    this.writeTileAt(this.tileOffset(tile), px);
  }

  /** Graphics tile `tile` (0..45) from the SHARED gfx block (extended font). */
  gfxTile(tile: number): RomFontTile {
    return this.readTileAt(this.gfxBase + tile * FONT_TILE_SIZE);
  }

  /** Patch graphics tile `tile` (0..45) in the SHARED gfx block. Affects every font's graphics. */
  setGfxTile(tile: number, px: readonly number[]): void {
    if (tile < 0 || tile >= FONT_GFX_TILE_COUNT || px.length < 64) return;
    this.writeTileAt(this.gfxBase + tile * FONT_TILE_SIZE, px);
  }

  /** Rebuild this font's inverted (+0x4D2) and shaded (+0x4D2·2) tile variants for tiles 2..70 from the
   *  main tiles — call after editing main tiles so LSDj's inverted/shaded UI renders right (port of
   *  lsdpatch generate{Inverted,Shaded}TileVariant). */
  regenerateVariants(): void {
    for (let t = 2; t < FONT_TILE_COUNT; t++) {
      const src = this.tileOffset(t);
      const inv = src + FONT_VARIANT_STRIDE;
      const shaded = src + FONT_VARIANT_STRIDE * 2;
      for (let i = 0; i < FONT_TILE_SIZE; i += 2) {
        // inverted: swap the two bitplane bytes and complement them
        this.writer.setU8(inv + i, ~this.view.u8(src + i + 1) & 0xff);
        this.writer.setU8(inv + i + 1, ~this.view.u8(src + i) & 0xff);
        // shaded: OR a dither mask into plane0 (alternating 0xAA/0x55 per row-pair), plane1 unchanged
        const srcByte = this.view.u8(src + i);
        this.writer.setU8(shaded + i, srcByte | (i % 4 === 2 ? 0xaa : 0x55));
        this.writer.setU8(shaded + i + 1, this.view.u8(src + i + 1));
      }
    }
  }

  toObject(): RomFont {
    const tiles: RomFontTile[] = [];
    for (let t = 0; t < FONT_TILE_COUNT; t++) tiles.push(this.tile(t));
    return { index: this.index, name: this.name, tiles };
  }
}
