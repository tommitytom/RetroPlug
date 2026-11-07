// The TS port of the old C++ `Rom::findOffset`: scan one bank for a byte pattern and return its
// absolute ROM offset (plus an addend), or -1 when absent. LSDj locates its font and palette sections
// this way — their in-ROM position drifts per version, but a distinctive byte marker at the section
// head is stable, so a scan finds them without a per-version offset table.
import { BANK_SIZE } from "./constants";

/** First offset in `bankIdx` where `pattern` matches, plus `addend`; -1 if not found or out of range. */
export function findPattern(rom: Uint8Array, bankIdx: number, pattern: readonly number[], addend = 0): number {
  const bankOffset = bankIdx * BANK_SIZE;
  if (pattern.length === 0 || bankOffset + BANK_SIZE > rom.length) return -1;
  const limit = bankOffset + BANK_SIZE - pattern.length;
  for (let i = bankOffset; i <= limit; i++) {
    let hit = true;
    for (let j = 0; j < pattern.length; j++) {
      if (rom[i + j] !== pattern[j]) {
        hit = false;
        break;
      }
    }
    if (hit) return i + addend;
  }
  return -1;
}

/** Scan EVERY bank for `pattern`; returns the first absolute offset (+addend) across the whole ROM, or -1. */
export function findPatternAnywhere(rom: Uint8Array, pattern: readonly number[], addend = 0): number {
  const banks = Math.floor(rom.length / BANK_SIZE);
  for (let b = 0; b < banks; b++) {
    const at = findPattern(rom, b, pattern, addend);
    if (at >= 0) return at;
  }
  return -1;
}
