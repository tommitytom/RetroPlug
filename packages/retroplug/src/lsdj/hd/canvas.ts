// The LSDj HD tile canvas - a TS port of the old C++ `lsdj::Canvas` (src/lsdj/LsdjCanvas.cpp on the
// ecs-linux branch). It draws LSDj's own 8x8 font glyphs, in LSDj's own palette, onto an arbitrarily large
// grid; the HD player uses a 97x72 grid (776x576 px) to show the song order, four chains and four phrases
// at once.
//
// The whole surface is a STRICT tile grid - every draw call in the original multiplies its coordinates by
// the tile size, and drawTile is a straight memcpy out of a pre-baked tile buffer. This port keeps that
// property and leans on it twice for speed, because a naive per-pixel port would be far too slow in
// QuickJS (97x72 tiles = ~447k pixel writes per frame at 60fps):
//
//   - Glyph atlas. Every (glyph, colour-set, dimmed) combination is baked once into `atlas` when the font
//     or palette changes, exactly like the original's `_tileBuffer`. Drawing is then a copy, never a
//     palette lookup.
//   - Tile-id shadow buffer. Draw calls only write atlas indices into `tiles`; `flush()` diffs that
//     against the last flushed frame and blits ONLY the tiles that changed. During playback just a handful
//     of tiles move per frame (the playback arrows and position numbers), so the steady-state cost is tiny
//     and the caller can skip pushing the buffer entirely when `flush()` reports nothing dirty.
//
// Pixels are XRGB8888 (0xAARRGGBB in a Uint32Array on a little-endian host), which is what both the
// emulator framebuffer (RpcFrame) and LVGL's Canvas.setBuffer expect, so the output needs no conversion.

import type { RomColorSet, RomFontTile } from "../rom/types";
import { ColorSets, COLOR_SET_COUNT, FONT_GLYPH_COUNT, findTile } from "./tiles";

const HEX_DIGITS = "0123456789ABCDEF";

export const TILE_WIDTH = 8;
export const TILE_HEIGHT = 8;
const TILE_PIXELS = TILE_WIDTH * TILE_HEIGHT;

// Atlas layout: 71 glyphs x 5 colour-sets, then the same again dimmed, then the solid-fill tiles (one per
// colour-set x palette index, for `fill`), then one all-black tile for `clear`.
const GLYPH_VARIANTS = FONT_GLYPH_COUNT * COLOR_SET_COUNT * 2;
const SOLID_BASE = GLYPH_VARIANTS;
const SOLID_COUNT = COLOR_SET_COUNT * 4;
const BLANK = SOLID_BASE + SOLID_COUNT;
const ATLAS_TILES = BLANK + 1;

const BLACK = 0xff000000;

/** Pack 8-bit RGB into the XRGB8888 word LVGL and the emulator framebuffers use. */
function packColor(r: number, g: number, b: number): number {
  return ((0xff << 24) | (r << 16) | (g << 8) | b) >>> 0;
}

// LSDj stores 4 colours per set but only treats 0 and 3 as the editable pair, blending between them for
// the middle shade - the original read exactly those two as `first`/`second` (Rom.h getPalette) and
// derived pixel value 1 as their average. Pixel 3 is unused by the font and falls back to `first`.
function colorForPixel(set: RomColorSet, pixel: number): number {
  const first = set.colors[0];
  const second = set.colors[3];
  switch (pixel) {
    case 1:
      return packColor((first.r + second.r) >> 1, (first.g + second.g) >> 1, (first.b + second.b) >> 1);
    case 2:
      return packColor(second.r, second.g, second.b);
    default:
      return packColor(first.r, first.g, first.b);
  }
}

export class LsdjHdCanvas {
  readonly cols: number;
  readonly rows: number;
  readonly width: number;
  readonly height: number;

  private readonly atlas = new Uint32Array(ATLAS_TILES * TILE_PIXELS);
  private readonly pixels: Uint32Array;
  /** Atlas index per grid cell for the frame being drawn. */
  private readonly tiles: Uint16Array;
  /** What `pixels` currently holds, so flush() can blit only the difference. */
  private readonly painted: Uint16Array;

  private font: RomFontTile[] | null = null;
  private palette: RomColorSet[] | null = null;
  /** Set when the atlas is rebuilt: every cell must be re-blitted even if its tile id is unchanged. */
  private atlasDirty = true;

  private tx = 0;
  private ty = 0;
  private readonly translationStack: number[] = [];

  constructor(cols: number, rows: number) {
    this.cols = cols;
    this.rows = rows;
    this.width = cols * TILE_WIDTH;
    this.height = rows * TILE_HEIGHT;
    this.pixels = new Uint32Array(this.width * this.height);
    this.tiles = new Uint16Array(cols * rows);
    this.painted = new Uint16Array(cols * rows);
    this.tiles.fill(BLANK);
    this.painted.fill(BLANK);
  }

  /** The rendered surface as XRGB8888 words. Pass `.buffer` to LVGL's Canvas.setBuffer. */
  getPixels(): Uint32Array {
    return this.pixels;
  }

  setFont(tiles: RomFontTile[]): void {
    this.font = tiles;
    this.rebuildAtlas();
  }

  setPalette(colorSets: RomColorSet[]): void {
    this.palette = colorSets;
    this.rebuildAtlas();
  }

  /** True once both a font and a palette have been supplied - drawing before that is a no-op. */
  get ready(): boolean {
    return this.font !== null && this.palette !== null;
  }

  // ---- atlas ---------------------------------------------------------------------

  private rebuildAtlas(): void {
    const { font, palette, atlas } = this;
    if (!font || !palette) return;

    // Colour-set index 0..4 is the plain set, 5..9 the dimmed copy of the same set. Dimming collapses the
    // darkest shade onto the mid shade, which is what the original's tile-buffer bake did. (It guarded on
    // `colorSetIdx > 5`, leaving dimmed-Normal identical to Normal - an off-by-one; nothing the HD player
    // draws is dimmed, so correcting it here changes no rendered output.)
    for (let variant = 0; variant < COLOR_SET_COUNT * 2; variant++) {
      const set = palette[variant % COLOR_SET_COUNT];
      const dimmed = variant >= COLOR_SET_COUNT;
      for (let glyph = 0; glyph < FONT_GLYPH_COUNT; glyph++) {
        const src = font[glyph];
        const base = (variant * FONT_GLYPH_COUNT + glyph) * TILE_PIXELS;
        for (let i = 0; i < TILE_PIXELS; i++) {
          let pixel = src ? src[i] : 0;
          if (dimmed && pixel === 2) pixel = 1;
          atlas[base + i] = colorForPixel(set, pixel);
        }
      }
    }

    for (let set = 0; set < COLOR_SET_COUNT; set++) {
      for (let paletteIdx = 0; paletteIdx < 4; paletteIdx++) {
        const color = colorForPixel(palette[set], paletteIdx);
        const base = (SOLID_BASE + set * 4 + paletteIdx) * TILE_PIXELS;
        atlas.fill(color, base, base + TILE_PIXELS);
      }
    }

    atlas.fill(BLACK, BLANK * TILE_PIXELS, (BLANK + 1) * TILE_PIXELS);
    this.atlasDirty = true;
  }

  // ---- translation ---------------------------------------------------------------

  translate(x: number, y: number): void {
    this.translationStack.push(this.tx, this.ty);
    this.tx += x;
    this.ty += y;
  }

  untranslate(): void {
    const y = this.translationStack.pop();
    const x = this.translationStack.pop();
    if (x === undefined || y === undefined) return;
    this.tx = x;
    this.ty = y;
  }

  /** Absolute translation. The original asserted an empty stack here; callers pair it with plain draws. */
  setTranslation(x: number, y: number): void {
    this.translationStack.length = 0;
    this.tx = x;
    this.ty = y;
  }

  // ---- drawing -------------------------------------------------------------------

  /** Reset every cell to black. renderMode2 opens with a full-surface fill, so this only matters between
   *  a font/palette swap and the first frame. */
  clear(): void {
    this.tiles.fill(BLANK);
  }

  private put(col: number, row: number, atlasIndex: number): void {
    // Out-of-range tiles are dropped, as in the original (which logged and skipped). renderMode2's 4th
    // phrase column overflows the right edge by up to two tiles on kit-instrument rows.
    if (col < 0 || row < 0 || col >= this.cols || row >= this.rows) return;
    this.tiles[row * this.cols + col] = atlasIndex;
  }

  fill(x: number, y: number, w: number, h: number, colorSet: ColorSets, paletteIdx: number): void {
    const id = SOLID_BASE + colorSet * 4 + paletteIdx;
    const x0 = x + this.tx;
    const y0 = y + this.ty;
    for (let row = y0; row < y0 + h; row++) {
      for (let col = x0; col < x0 + w; col++) this.put(col, row, id);
    }
  }

  drawTile(x: number, y: number, tile: number, colorSet: ColorSets, dimmed = false): void {
    const variant = dimmed ? colorSet + COLOR_SET_COUNT : colorSet;
    this.put(x + this.tx, y + this.ty, variant * FONT_GLYPH_COUNT + tile);
  }

  /** Draw a string, one glyph per tile. Lower-case is folded up (the font has no lower-case glyphs);
   *  unmapped characters blank to a space. */
  text(x: number, y: number, str: string, colorSet: ColorSets, dimmed = false): void {
    for (let i = 0; i < str.length; i++) {
      let code = str.charCodeAt(i);
      if (code >= 0x61 && code <= 0x7a) code -= 0x20; // a-z → A-Z
      this.drawTile(x + i, y, findTile(code), colorSet, dimmed);
    }
  }

  /** A byte in hex. `pad` forces two digits; without it, small values render as a single digit. (The
   *  original's threshold is `value >= 15`, so 0x0F renders padded either way - preserved.) */
  hexNumber(x: number, y: number, value: number, colorSet: ColorSets, pad = true, dimmed = false): void {
    const v = value & 0xff;
    const hi = HEX_DIGITS.charCodeAt(v >> 4);
    const lo = HEX_DIGITS.charCodeAt(v & 0xf);
    if (v >= 15 || pad) {
      this.drawTile(x, y, findTile(hi), colorSet, dimmed);
      this.drawTile(x + 1, y, findTile(lo), colorSet, dimmed);
    } else {
      this.drawTile(x, y, findTile(lo), colorSet, dimmed);
    }
  }

  /** A byte in decimal, zero-padded to 3 digits unless `pad` is cleared. */
  number(x: number, y: number, value: number, colorSet: ColorSets, pad = true, dimmed = false): void {
    const s = pad ? String(value & 0xff).padStart(3, "0") : String(value & 0xff);
    this.text(x, y, s, colorSet, dimmed);
  }

  // ---- present -------------------------------------------------------------------

  /** Blit every cell whose tile changed since the last flush into the pixel buffer. Returns how many were
   *  repainted, so the caller can skip pushing an unchanged surface (LVGL's setBuffer copies the whole
   *  thing and forces a full invalidate). */
  flush(): number {
    const { tiles, painted, pixels, atlas, cols, rows, width } = this;
    const forceAll = this.atlasDirty;
    this.atlasDirty = false;

    let dirty = 0;
    for (let row = 0; row < rows; row++) {
      const rowBase = row * cols;
      const pixelRowBase = row * TILE_HEIGHT * width;
      for (let col = 0; col < cols; col++) {
        const id = tiles[rowBase + col];
        if (!forceAll && painted[rowBase + col] === id) continue;
        painted[rowBase + col] = id;
        dirty++;

        const src = id * TILE_PIXELS;
        const dst = pixelRowBase + col * TILE_WIDTH;
        for (let ty = 0; ty < TILE_HEIGHT; ty++) {
          const s = src + ty * TILE_WIDTH;
          const d = dst + ty * width;
          pixels[d] = atlas[s];
          pixels[d + 1] = atlas[s + 1];
          pixels[d + 2] = atlas[s + 2];
          pixels[d + 3] = atlas[s + 3];
          pixels[d + 4] = atlas[s + 4];
          pixels[d + 5] = atlas[s + 5];
          pixels[d + 6] = atlas[s + 6];
          pixels[d + 7] = atlas[s + 7];
        }
      }
    }
    return dirty;
  }

  /** The grid as atlas indices - a compact, diffable form for golden tests. */
  snapshotTiles(): Uint16Array {
    return this.tiles.slice();
  }
}
