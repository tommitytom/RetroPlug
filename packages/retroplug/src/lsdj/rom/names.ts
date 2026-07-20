// LSDj font + palette NAMES and the palette COUNT. All three anchor on the "grayscale palette names"
// landmark in bank 27 — a run of three 5-byte name slots (4 non-zero chars + a NUL). Ported from
// lsdpatch's RomUtilities (jkotlinski): the definitive way LSDj stores these, robust across versions and
// custom ROMs. Names are 5-byte slots (≤4 printable chars, NUL-terminated).
import { BANK_SIZE, FONT_COUNT, NAME_BANK, PALETTE_COUNT, PALETTE_NAME_SIZE } from "./constants";

function readSlot(rom: Uint8Array, off: number): string {
  let s = "";
  for (let i = 0; i < PALETTE_NAME_SIZE; i++) {
    const c = rom[off + i];
    if (c === 0) break;
    if (c >= 0x20 && c <= 0x7e) s += String.fromCharCode(c);
  }
  return s.trimEnd();
}

/** Offset just past the 3-slot grayscale-palette-name landmark in bank 27, or -1. The font-name and
 *  palette-name tables + the palette count are all measured from here (matches lsdpatch). */
export function findGrayscaleNames(rom: Uint8Array): number {
  const start = NAME_BANK * BANK_SIZE;
  const end = Math.min(rom.length, (NAME_BANK + 1) * BANK_SIZE);
  const nz = (o: number): boolean => rom[o] !== 0;
  for (let i = start; i + 15 <= end; i++) {
    // three slots of (4 non-zero, 1 zero)
    if (nz(i) && nz(i + 1) && nz(i + 2) && nz(i + 3) && rom[i + 4] === 0 &&
        nz(i + 5) && nz(i + 6) && nz(i + 7) && nz(i + 8) && rom[i + 9] === 0 &&
        nz(i + 10) && nz(i + 11) && nz(i + 12) && nz(i + 13) && rom[i + 14] === 0) {
      return i + 15;
    }
  }
  return -1;
}

/** Number of colour palettes in the ROM, derived from the run of valid name slots after the landmark
 *  (lsdpatch: count-of-valid-slots / 2). Falls back to PALETTE_COUNT when the landmark isn't present. */
export function paletteCount(rom: Uint8Array): number {
  const base = findGrayscaleNames(rom);
  if (base < 0) return PALETTE_COUNT;
  let n = 0;
  for (let j = base + 4; j < rom.length && rom[j] === 0; j += PALETTE_NAME_SIZE) n++;
  return n >> 1;
}

/** The `count` palette names (each ≤4 chars). Empty when the landmark isn't found. */
export function paletteNames(rom: Uint8Array, count: number): string[] {
  const base = findGrayscaleNames(rom);
  if (base < 0) return [];
  const off = base + PALETTE_NAME_SIZE * count; // the palette-name table sits after the grayscale block
  return Array.from({ length: count }, (_, i) => readSlot(rom, off + i * PALETTE_NAME_SIZE));
}

/** The 3 font names (each ≤4 chars), stored just before the landmark. Empty when not found. */
export function fontNames(rom: Uint8Array): string[] {
  const base = findGrayscaleNames(rom);
  if (base < 0) return [];
  const off = base - PALETTE_NAME_SIZE * FONT_COUNT;
  return Array.from({ length: FONT_COUNT }, (_, i) => readSlot(rom, off + i * PALETTE_NAME_SIZE));
}

// Write a 4-char name into a 5-byte slot: uppercased, truncated, SPACE-padded to 4 (lsdpatch's
// setPaletteName/setFontName convention); the 5th byte (the NUL terminator) is left untouched.
function writeSlot(rom: Uint8Array, off: number, name: string): void {
  const up = name.toUpperCase();
  for (let i = 0; i < PALETTE_NAME_SIZE - 1; i++) {
    const c = i < up.length ? up.charCodeAt(i) : 0x20; // space pad
    rom[off + i] = c >= 0x20 && c <= 0x7e ? c : 0x20;
  }
}

/** Rename palette `index` (of `count`) in place, in the bank-27 name table. No-op if the landmark's
 *  missing or the index is out of range. Mirrors lsdpatch RomUtilities.setPaletteName. */
export function setPaletteName(rom: Uint8Array, index: number, count: number, name: string): void {
  const base = findGrayscaleNames(rom);
  if (base < 0 || index < 0 || index >= count) return;
  writeSlot(rom, base + PALETTE_NAME_SIZE * count + index * PALETTE_NAME_SIZE, name);
}

/** Rename font `index` (0..2) in place, in the bank-27 name table. No-op if the landmark's missing or the
 *  index is out of range. Mirrors lsdpatch RomUtilities.setFontName. */
export function setFontName(rom: Uint8Array, index: number, name: string): void {
  const base = findGrayscaleNames(rom);
  if (base < 0 || index < 0 || index >= FONT_COUNT) return;
  writeSlot(rom, base - PALETTE_NAME_SIZE * FONT_COUNT + index * PALETTE_NAME_SIZE, name);
}
