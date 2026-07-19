// LSDj colour palettes. The palette block is located by scanning bank 1 for PALETTE_CHECK, which sits
// at the END of the block: base = firstMatch - PALETTE_COUNT * PALETTE_SIZE (the old Rom.cpp's
// `findOffset(1, PALETTE_CHECK, -240)`). Each of the 6 palettes is 40 bytes = 5 colour-sets × 8 bytes,
// and each colour-set is a full GBC 4-colour palette (4 × little-endian 15-bit RGB555).
import { BitView, BitWriter } from "../codec/bits";
import { findPattern } from "./find";
import type { RomColor, RomColorSet, RomPalette } from "./types";
import {
  PALETTE_CHECK,
  PALETTE_COLOR_SET_COUNT,
  PALETTE_COLOR_SET_SIZE,
  PALETTE_SIZE,
} from "./constants";

const PALETTE_BANK = 1;

/** RGB555 word → 8-bit-per-channel colour (the standard 5→8 bit expansion, replicating the top bits). */
export function unpackRgb555(word: number): RomColor {
  const r = word & 0x1f;
  const g = (word >> 5) & 0x1f;
  const b = (word >> 10) & 0x1f;
  return { r: (r << 3) | (r >> 2), g: (g << 3) | (g >> 2), b: (b << 3) | (b >> 2) };
}

/** 8-bit-per-channel colour → RGB555 word (truncate to the top 5 bits of each channel). */
export function packRgb555(c: RomColor): number {
  const r = (c.r >> 3) & 0x1f;
  const g = (c.g >> 3) & 0x1f;
  const b = (c.b >> 3) & 0x1f;
  return r | (g << 5) | (b << 10);
}

const LSDPAL_NAME_LEN = 4;
const LSDPAL_FILE_SIZE = LSDPAL_NAME_LEN + PALETTE_SIZE; // 44

/** Decode a `.lsdpal` file (4-char name + 40-byte palette body) to a name + STRUCTURED colour-sets — the
 *  form a palette override is stored as (readable JSON, never binary/base64). `null` on a wrong size. */
export function decodeLsdpal(file: Uint8Array): { name: string; colorSets: RomColorSet[] } | null {
  if (file.length !== LSDPAL_FILE_SIZE) return null;
  let name = "";
  for (let i = 0; i < LSDPAL_NAME_LEN; i++) {
    const c = file[i];
    if (c >= 0x20 && c <= 0x7e) name += String.fromCharCode(c);
  }
  const view = new BitView(file);
  const colorSets: RomColorSet[] = [];
  for (let s = 0; s < PALETTE_COLOR_SET_COUNT; s++) {
    const colors: RomColor[] = [];
    for (let c = 0; c < 4; c++) colors.push(unpackRgb555(view.u16le(LSDPAL_NAME_LEN + s * PALETTE_COLOR_SET_SIZE + c * 2)));
    colorSets.push({ colors });
  }
  return { name: name.trimEnd(), colorSets };
}

/** Re-encode a stored palette override (name + colour-sets) back to a `.lsdpal` file (44 bytes). RGB555 is
 *  lossy in 8-bit but the pack∘unpack of decodeLsdpal is word-exact, so a decode→encode round-trips. */
export function encodeLsdpal(name: string, colorSets: RomColorSet[]): Uint8Array {
  const out = new Uint8Array(LSDPAL_FILE_SIZE);
  const up = name.toUpperCase();
  for (let i = 0; i < LSDPAL_NAME_LEN; i++) {
    const c = i < up.length ? up.charCodeAt(i) : 0x20;
    out[i] = c >= 0x20 && c <= 0x7e ? c : 0x20;
  }
  const writer = new BitWriter(out);
  for (let s = 0; s < PALETTE_COLOR_SET_COUNT; s++) {
    for (let c = 0; c < 4; c++) writer.setU16le(LSDPAL_NAME_LEN + s * PALETTE_COLOR_SET_SIZE + c * 2, packRgb555(colorSets[s]?.colors[c] ?? { r: 0, g: 0, b: 0 }));
  }
  return out;
}

// The PALETTE_CHECK marker sits at the END of the colour block, so the block base is `count` palettes
// before it (the old Rom.cpp's `findOffset(1, PALETTE_CHECK, -count*40)`). `count` comes from the bank-27
// name table (names.paletteCount) — 9.4.2 has 7, not the old hard-coded 6.
/** The absolute ROM offset of the palette block (palette 0, colour-set 0) for `count` palettes, or -1. */
export function findPaletteBase(rom: Uint8Array, count: number): number {
  return findPattern(rom, PALETTE_BANK, PALETTE_CHECK, -(count * PALETTE_SIZE));
}

export class PaletteView {
  private readonly view: BitView;
  private readonly writer: BitWriter;

  /** `base` = findPaletteBase(rom, count); `name` from names.paletteNames(). Via LsdjRom.palettes(). */
  constructor(rom: Uint8Array, private readonly base: number, readonly index: number, readonly name = "") {
    this.view = new BitView(rom);
    this.writer = new BitWriter(rom);
  }

  private colorOffset(set: number, color: number): number {
    return this.base + this.index * PALETTE_SIZE + set * PALETTE_COLOR_SET_SIZE + color * 2;
  }

  /** The palette's raw 40 bytes (5 colour-sets × 8) — the body of a `.lsdpal` file. */
  raw(): Uint8Array {
    return this.view.slice(this.base + this.index * PALETTE_SIZE, PALETTE_SIZE);
  }

  /** Overwrite the palette's 40 bytes in place (from a `.lsdpal` body). Ignores a wrong-sized buffer. */
  setRaw(bytes: Uint8Array): void {
    if (bytes.length !== PALETTE_SIZE) return;
    for (let i = 0; i < PALETTE_SIZE; i++) this.writer.setU8(this.base + this.index * PALETTE_SIZE + i, bytes[i]);
  }

  /** Colour `color` (0..3) of colour-set `set` (0..4). */
  color(set: number, color: number): RomColor {
    return unpackRgb555(this.view.u16le(this.colorOffset(set, color)));
  }

  /** Patch colour `color` (0..3) of colour-set `set` (0..4) in place (packed to RGB555). */
  setColor(set: number, color: number, c: RomColor): void {
    if (set < 0 || set >= PALETTE_COLOR_SET_COUNT || color < 0 || color >= 4) return;
    this.writer.setU16le(this.colorOffset(set, color), packRgb555(c));
  }

  toObject(): RomPalette {
    const colorSets: RomColorSet[] = [];
    for (let s = 0; s < PALETTE_COLOR_SET_COUNT; s++) {
      const colors: RomColor[] = [];
      for (let c = 0; c < 4; c++) colors.push(this.color(s, c));
      colorSets.push({ colors });
    }
    return { index: this.index, name: this.name, colorSets };
  }
}
