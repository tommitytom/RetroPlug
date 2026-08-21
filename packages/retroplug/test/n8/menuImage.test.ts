// The N8 menu-screen assembler (src/n8/menuImage.ts): NES nametable pairs + 2bpp CHR + palette -> 256x224
// RGBA. Pure, no hardware. Synths a couple of known tiles to check the port of edlink MenuImage.MakeImage:
// the 2bpp bit order, attr bits 0-1 = palette group, attr bit 2 = 2nd-CHR-bank select, and the palette->
// NES-master-palette->RGB chain.
import { test, expect } from "../../testing/harness";
import { menuScreenToRgba } from "../../src/n8/menuImage";

// NES master-palette indices (into edlink's pal_nes): 0x20 = white (ff,ff,ff), 0x0F = black (00,00,00).
const WHITE = 0x20;
const BLACK = 0x0f;

const px = (rgba: Uint8Array, x: number, y: number): number[] => {
  const o = (y * 256 + x) * 4;
  return [rgba[o], rgba[o + 1], rgba[o + 2], rgba[o + 3]];
};

test("menuScreenToRgba renders 256x224 with the right tile/attr/palette decode", () => {
  const chr = new Uint8Array(8192);
  const vram = new Uint8Array(2048);
  const palette = new Uint8Array(16);

  // CHR tile 1: plane0 row 0 = 0xFF, plane1 row 0 = 0x00 -> pattern value 1 across the whole top row.
  chr[1 * 16 + 0] = 0xff;
  // CHR tile 256 (only reachable via attr bit-2 bank select): same top-row pattern-value-1.
  chr[256 * 16 + 0] = 0xff;

  // palette groups: idx4 = (attr&3)<<2 | patternValue.
  palette[5] = WHITE; // group 1, pattern 1  (tile (0,0))
  palette[4] = BLACK; // group 1, pattern 0
  palette[1] = WHITE; // group 0, pattern 1  (tile (1,0), via bank select)
  palette[0] = BLACK; // group 0, pattern 0  (background)

  // grid (0,0): tile 1, attr 1 (palette group 1)
  vram[0] = 1;
  vram[1] = 1;
  // grid (1,0): tile 0 + attr 4 (bank select -> tile 256), attr group 0
  vram[2] = 0;
  vram[3] = 4;
  // every other grid cell stays (tile 0, attr 0) -> background black

  const img = menuScreenToRgba(chr, vram, palette);

  expect(img.width).toBe(256);
  expect(img.height).toBe(224);
  expect(img.rgba.length).toBe(256 * 224 * 4);

  // tile (0,0) row 0: pattern 1, group 1 -> palette[5] = white; every x in the 8px tile row.
  expect(px(img.rgba, 0, 0)).toEqual([255, 255, 255, 255]);
  expect(px(img.rgba, 7, 0)).toEqual([255, 255, 255, 255]);
  // tile (0,0) row 1: pattern 0 (chr rows 1-7 zero) -> palette[4] = black.
  expect(px(img.rgba, 0, 1)).toEqual([0, 0, 0, 255]);
  // tile (1,0) row 0 via attr bit-2 bank select (tile 256): pattern 1, group 0 -> palette[1] = white.
  // (If bank select were ignored, tile 0 would render -> black, so this pixel proves attr&4.)
  expect(px(img.rgba, 8, 0)).toEqual([255, 255, 255, 255]);
  // background cell (2,0): tile 0, attr 0, pattern 0 -> palette[0] = black.
  expect(px(img.rgba, 16, 0)).toEqual([0, 0, 0, 255]);
});
