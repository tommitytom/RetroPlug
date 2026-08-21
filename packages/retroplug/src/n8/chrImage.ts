// NES CHR <-> grayscale-PNG codec for the N8 live-patch loop: dump a running game's CHR bank as an editable
// grayscale tile-grid image, edit a glyph in any image editor, and encode it back to CHR bytes to memWR over
// USB (see cli/sessions/n8-load.ts --dump-chr / --patch-chr). Pure + host-agnostic like menuImage.ts/sniffer.ts
// (decoding, not protocol - no C++ twin). Reuses the NES planar-2bpp tile codec from risa/rom (decodeTile/
// encodeTile) and lays tiles out row-major exactly like the LSDj font-image layer.
//
// Each CHR pixel is a 2bpp value 0-3 (a palette index, not a brightness). We render it to a fixed 4-level gray
// ramp - pixel 0 = white "paper" .. 3 = black "ink" - so the roundtrip is lossless (dump then patch is a no-op)
// and an edited gray buckets to the nearest of the 4 levels. This is a grayscale editing view, NOT the game's
// real colours (those depend on the live PPU palette; a colour view is a later nicety).

import { decodeTile, encodeTile, TILE_BYTES, NES_PALETTE } from "../risa/rom";

type Rgb = readonly [number, number, number];
/** RGB for a NES master-palette index (0-63) from risa's NES_PALETTE ("#rrggbb"). */
export function nesRgb(nesIndex: number): Rgb {
  const h = NES_PALETTE[nesIndex & 0x3f];
  return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
}

/** The 4 CHR pixel values (0-3) as gray levels: 0 = white (paper) .. 3 = black (ink). */
export const SHADE4 = [0xff, 0xaa, 0x55, 0x00] as const;

/** A raw RGBA8888 image (rgba is width*height*4 bytes, row-major, top-to-bottom) - matches PngImageData. */
export interface ChrImage {
  width: number;
  height: number;
  rgba: Uint8Array;
}

/** Map a gray level (0-255) to the nearest CHR pixel value (0-3). Inverse of SHADE4 (thresholds at the four
 *  grays' midpoints): 255->0, 170->1, 85->2, 0->3. */
export function grayToPixel(gray: number): number {
  const v = Math.round((255 - gray) / 85);
  return v < 0 ? 0 : v > 3 ? 3 : v;
}

/** Lay a CHR bank out as a tile grid (`tilesWide` tiles per row; 512 tiles / 16 = 128x256), colouring each
 *  pixel (0-3) via `pixelRgb`. The shared core of the grayscale + colour renderers. */
function renderChr(bank: Uint8Array, tilesWide: number, pixelRgb: (v: number) => Rgb): ChrImage {
  const tileCount = Math.floor(bank.length / TILE_BYTES);
  const rows = Math.max(1, Math.ceil(tileCount / tilesWide));
  const width = tilesWide * 8;
  const height = rows * 8;
  const rgba = new Uint8Array(width * height * 4);
  for (let i = 3; i < rgba.length; i += 4) rgba[i] = 0xff; // opaque

  for (let t = 0; t < tileCount; t++) {
    const px = decodeTile(bank, t); // 64 values 0-3, row-major
    const baseX = (t % tilesWide) * 8;
    const baseY = Math.floor(t / tilesWide) * 8;
    for (let y = 0; y < 8; y++) {
      for (let x = 0; x < 8; x++) {
        const [r, g, b] = pixelRgb(px[y * 8 + x] & 3);
        const o = ((baseY + y) * width + (baseX + x)) * 4;
        rgba[o] = r;
        rgba[o + 1] = g;
        rgba[o + 2] = b;
      }
    }
  }
  return { width, height, rgba };
}

/** Render a CHR bank to a grayscale tile-grid image (editable, lossless roundtrip via pngToChr). */
export function chrToPng(bank: Uint8Array, tilesWide = 16): ChrImage {
  return renderChr(bank, tilesWide, (v) => [SHADE4[v], SHADE4[v], SHADE4[v]]);
}

/** Render a CHR bank in the game's REAL colours, using a live 32-byte PPU palette (from the sniffer) and BG
 *  palette group `group` (0-3): pixel 0 = universal background ($3F00), 1-3 = the group's colours. A viewing
 *  aid (not roundtrip-editable) - pair with a running-game sniffer palette read. */
export function chrToPngColor(bank: Uint8Array, paletteRam: Uint8Array, group = 0, tilesWide = 16): ChrImage {
  const g = (group & 3) * 4;
  const colors: Rgb[] = [nesRgb(paletteRam[0]), nesRgb(paletteRam[g + 1]), nesRgb(paletteRam[g + 2]), nesRgb(paletteRam[g + 3])];
  return renderChr(bank, tilesWide, (v) => colors[v]);
}

/** Encode a grayscale tile-grid image back to CHR bytes (tileCount = (width/8)*(height/8) tiles, planar 2bpp).
 *  Each pixel maps by brightness (max of R/G/B) to the nearest of the 4 CHR values, so a lossless roundtrip of
 *  a chrToPng image reproduces the exact bank. */
export function pngToChr(img: ChrImage): Uint8Array {
  if (img.width % 8 !== 0 || img.height % 8 !== 0)
    throw new Error(`CHR image must be a multiple of 8px in each dimension (got ${img.width}x${img.height})`);
  if (img.rgba.length < img.width * img.height * 4)
    throw new Error(`CHR image rgba too small: ${img.rgba.length} < ${img.width * img.height * 4}`);

  const tilesWide = img.width / 8;
  const tileCount = tilesWide * (img.height / 8);
  const bank = new Uint8Array(tileCount * TILE_BYTES);
  for (let t = 0; t < tileCount; t++) {
    const baseX = (t % tilesWide) * 8;
    const baseY = Math.floor(t / tilesWide) * 8;
    const px: number[] = [];
    for (let y = 0; y < 8; y++) {
      for (let x = 0; x < 8; x++) {
        const o = ((baseY + y) * img.width + (baseX + x)) * 4;
        const gray = Math.max(img.rgba[o], img.rgba[o + 1], img.rgba[o + 2]);
        px.push(grayToPixel(gray));
      }
    }
    encodeTile(bank, t, px);
  }
  return bank;
}
