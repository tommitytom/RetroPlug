// Pure-TS kit-bank assembly, used to SPLICE a kit (single-sample import / remove) without re-encoding untouched
// samples: read existing samples as raw nibble bytes (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank
// here. The TypeScript kit compiler (src/kit/) assembles every bank it compiles here too, whatever host it runs in.
// The layout matches kit.ts's:
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

const utf8 = new TextEncoder();

// Write a fixed-width name: the name's bytes (its UTF-8), each a-z uppercased and every other byte as it is, truncated
// to `size` and padded with `fill`. The byte rule is the bank layout's, not a locale's, so one name gives one bank: a
// name of printable ASCII (all a kit read back from a ROM can have, KitView's readName) is the same under any rule, and
// test/kit/kit.test.ts's non-ASCII case pins what these bytes are for anything else.
function writeField(bank: Uint8Array, off: number, size: number, name: string, fill: number): void {
  const bytes = utf8.encode(name);
  for (let i = 0; i < size; i++) {
    const c = i < bytes.length ? bytes[i] : fill;
    bank[off + i] = c >= 0x61 && c <= 0x7a ? c - 0x20 : c;
  }
}

/** The total sample-data bytes `samples` need (to pre-check against KIT_MAX_SAMPLE_SPACE = 0x3fa0). */
export function kitSampleSpace(samples: readonly KitSample[]): number {
  return samples.slice(0, KIT_MAX_SAMPLES).reduce((n, s) => n + s.bytes.length, 0);
}

/** A bank and where each sample landed in it: `written[i]` is how many of sample i's bytes the bank holds (fewer
 *  than it has when the 0x3fa0-byte budget ran out part way through it), or null for a sample that found no room at
 *  all and got the empty-slot marker. A sample past the 15 slots is not in `written`. */
export interface PlacedKitBank {
  bank: Uint8Array;
  written: (number | null)[];
}

/** Assemble a 16 KB kit bank from a kit name + samples (≤15 honored). Sample data past the 0x3fa0 budget
 *  is clipped - callers pre-check with kitSampleSpace to fail loudly. */
export function buildKitBank(kitName: string, samples: readonly KitSample[]): Uint8Array {
  return buildKitBankPlaced(kitName, samples).bank;
}

/** buildKitBank, saying where each sample landed: the kit compiler's per-slot reason for a sample the bank does not
 *  hold whole comes from here, so it cannot drift from the assembler's own fit rule. */
export function buildKitBankPlaced(kitName: string, samples: readonly KitSample[]): PlacedKitBank {
  const bank = new Uint8Array(BANK_SIZE);
  const written: (number | null)[] = [];
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
      written.push(writeSize);
    } else {
      bank[nameOff] = 0; // empty-slot sentinel: name[0]=0, then '--'
      bank[nameOff + 1] = 0x2d;
      bank[nameOff + 2] = 0x2d;
      setEntry(i + 1, 0);
      if (i < samples.length) written.push(null);
    }
  }
  return { bank, written };
}

/** Extract sample `i`'s raw nibble bytes from a standalone 16 KB bank (base 0) - e.g. a 1-sample bank
 *  from compileKit's result, to splice into an existing kit. Empty when the slot is unused. */
export function sampleBytesFromBank(bank: Uint8Array, i: number): Uint8Array {
  const entry = (k: number): number => bank[k * 2] | (bank[k * 2 + 1] << 8);
  if (i < 0 || i >= KIT_MAX_SAMPLES) return new Uint8Array(0);
  const start = entry(i);
  const end = entry(i + 1);
  if (start < KIT_VALID_MAGIC || end < start) return new Uint8Array(0);
  return bank.slice(start - 0x4000, end - 0x4000);
}
