// Pure-TS kit-bank assembly — the port of native KitUtil::buildKit, used to SPLICE a kit (single-sample
// import / remove) without re-encoding untouched samples: read existing samples as raw nibble bytes
// (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank here. Whole-kit builds go through the
// native compileKit instead (it does resample + assembly in parallel). Layout matches kit.ts / KitUtil:
// a 16-entry u16le offset table (entry 0 = 0x4060; entries i+1 = each sample's end GB-addr; 0 terminates),
// 15 × 3-char sample names at 0x22, a 6-char kit name at 0x52, sample data from 0x60 (≤ 0x3fa0 bytes).
import {
  BANK_SIZE,
  KIT_MAX_SAMPLES,
  KIT_MAX_SAMPLE_SPACE,
  KIT_NAME_OFFSET,
  KIT_NAME_SIZE,
  KIT_SAMPLE_DATA_OFFSET,
  KIT_SAMPLE_NAME_OFFSET,
  KIT_SAMPLE_NAME_SIZE,
  KIT_VALID_MAGIC,
} from "./constants";

export interface KitSample {
  name: string;
  bytes: Uint8Array; // raw 4-bit nibble stream (already frame-aligned by the compiler)
}

// Write a fixed-width name: uppercase ASCII, truncated to `size`, padded with `fill` to the end.
function writeField(bank: Uint8Array, off: number, size: number, name: string, fill: number): void {
  const up = name.toUpperCase();
  for (let i = 0; i < size; i++) {
    const c = i < up.length ? up.charCodeAt(i) : fill;
    bank[off + i] = c >= 0x20 && c <= 0x7e ? c : fill;
  }
}

/** The total sample-data bytes `samples` need (to pre-check against KIT_MAX_SAMPLE_SPACE = 0x3fa0). */
export function kitSampleSpace(samples: readonly KitSample[]): number {
  return samples.slice(0, KIT_MAX_SAMPLES).reduce((n, s) => n + s.bytes.length, 0);
}

/** Assemble a 16 KB kit bank from a kit name + samples (≤15 honored). Sample data past the 0x3fa0 budget
 *  is clipped (matches native KitUtil::buildKit) — callers pre-check with kitSampleSpace to fail loudly. */
export function buildKitBank(kitName: string, samples: readonly KitSample[]): Uint8Array {
  const bank = new Uint8Array(BANK_SIZE);
  writeField(bank, KIT_NAME_OFFSET, KIT_NAME_SIZE, kitName, 0x20); // kit name, space-padded

  const setEntry = (i: number, gbAddr: number): void => {
    bank[i * 2] = gbAddr & 0xff;
    bank[i * 2 + 1] = (gbAddr >> 8) & 0xff;
  };
  setEntry(0, KIT_VALID_MAGIC); // 0x4060 — sample-data start + "valid" magic

  let cursor = 0;
  let remaining = KIT_MAX_SAMPLE_SPACE;
  for (let i = 0; i < KIT_MAX_SAMPLES; i++) {
    const nameOff = KIT_SAMPLE_NAME_OFFSET + i * KIT_SAMPLE_NAME_SIZE;
    if (i < samples.length && remaining > 0) {
      const src = samples[i].bytes;
      const writeSize = Math.min(src.length, remaining);
      remaining -= writeSize;
      writeField(bank, nameOff, KIT_SAMPLE_NAME_SIZE, samples[i].name, 0x2d); // '-'-padded
      bank.set(src.subarray(0, writeSize), KIT_SAMPLE_DATA_OFFSET + cursor);
      cursor += writeSize;
      setEntry(i + 1, KIT_VALID_MAGIC + cursor); // end GB-addr
    } else {
      bank[nameOff] = 0; // empty-slot sentinel: name[0]=0, then '--'
      bank[nameOff + 1] = 0x2d;
      bank[nameOff + 2] = 0x2d;
      setEntry(i + 1, 0);
    }
  }
  return bank;
}

/** Extract sample `i`'s raw nibble bytes from a standalone 16 KB bank (base 0) — e.g. a 1-sample bank
 *  returned by compileKit, to splice into an existing kit. Empty when the slot is unused. */
export function sampleBytesFromBank(bank: Uint8Array, i: number): Uint8Array {
  const entry = (k: number): number => bank[k * 2] | (bank[k * 2 + 1] << 8);
  if (i < 0 || i >= KIT_MAX_SAMPLES) return new Uint8Array(0);
  const start = entry(i);
  const end = entry(i + 1);
  if (start < KIT_VALID_MAGIC || end < start) return new Uint8Array(0);
  return bank.slice(start - 0x4000, end - 0x4000);
}
