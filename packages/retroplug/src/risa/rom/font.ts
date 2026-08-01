// risa font (CHR) helpers. A font slot is one 8 KB CHR bank of 512 NES 2bpp tiles. Replace is whole-bank
// (the .chr interop file is exactly the raw 8192 bytes — no header), handled by RisaRom.setChrFontSlot.
// The tile codec here is PLANAR (bytes 0-7 = bitplane 0 rows 0-7, bytes 8-15 = bitplane 1) — unlike GB's
// interleaved row pairs — and exists for inspection/tests. Ported from risa's font_editor/model.js.

import { TILE_BYTES } from "./constants";

/** Decode one 8x8 tile from a CHR bank into 64 pixel values (0..3), row-major. */
export function decodeTile(bank: Uint8Array, tileIdx: number): number[] {
  const off = tileIdx * TILE_BYTES;
  const px: number[] = [];
  for (let y = 0; y < 8; y++) {
    const p0 = bank[off + y];
    const p1 = bank[off + y + 8];
    for (let x = 0; x < 8; x++) {
      const bit = 7 - x;
      px.push(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1));
    }
  }
  return px;
}

/** Encode 64 pixel values (0..3) into one tile's 16 planar bytes, in place in `bank`. */
export function encodeTile(bank: Uint8Array, tileIdx: number, pixels: number[]): void {
  const off = tileIdx * TILE_BYTES;
  for (let y = 0; y < 8; y++) {
    let p0 = 0;
    let p1 = 0;
    for (let x = 0; x < 8; x++) {
      const v = pixels[y * 8 + x] & 3;
      const bit = 7 - x;
      if (v & 1) p0 |= 1 << bit;
      if (v & 2) p1 |= 1 << bit;
    }
    bank[off + y] = p0;
    bank[off + y + 8] = p1;
  }
}
