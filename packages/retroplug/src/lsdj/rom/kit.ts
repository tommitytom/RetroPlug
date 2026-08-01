// A view over one LSDj kit (a whole ROM bank of drum samples). A valid kit starts with a 16-entry
// u16le offset table: entry 0 is the sample-data start as a GB address (0x4060, the "valid" magic),
// entries 1.. are each sample's END address, and a 0x0000 entry terminates the list. Sample audio is
// 4-bit nibble PCM (2 samples/byte, high nibble first) living from in-bank 0x60 onward. Names: a 6-char
// kit name at 0x52 and up to 15 × 3-char sample names at 0x22.
import { BitView, BitWriter } from "../codec/bits";
import type { RomKit, RomSample } from "./types";
import {
  BANK_SIZE,
  BYTES_PER_FRAME,
  KIT_EMPTY_MAGIC,
  KIT_LOOKUP,
  KIT_MAX_SAMPLES,
  KIT_NAME_OFFSET,
  KIT_NAME_SIZE,
  KIT_OFFSET_ENTRIES,
  KIT_SAMPLE_NAME_OFFSET,
  KIT_SAMPLE_NAME_SIZE,
  KIT_VALID_MAGIC,
  SAMPLES_PER_FRAME,
} from "./constants";

// Decode a fixed-width name field to trimmed ASCII (LSDj pads with NUL; treat 0xFF padding as blank too).
function readName(view: BitView, off: number, size: number): string {
  let s = "";
  for (let i = 0; i < size; i++) {
    const c = view.u8(off + i);
    if (c === 0 || c === 0xff) break;
    if (c >= 0x20 && c <= 0x7e) s += String.fromCharCode(c);
  }
  return s.trimEnd();
}

// Decode packed 4-bit kit nibbles → PCM in [-1, 1]. Nibbles are stored INVERTED (amp = 0xF - stored); a
// byte holds two samples (high nibble first). When `rotate` (LSDj 9.2.0+), each 32-sample frame was
// stored rotated right by one — undo it by reading encoded position (i+1)%32. Port of native
// SampleUtil.hpp's convertNibblesToF32 / convertNibblesToF32WithRotation.
export function decodeNibbles(bytes: Uint8Array, rotate: boolean): Float32Array {
  const amp = (n: number): number => ((0xf - n) / 15) * 2 - 1;
  if (!rotate) {
    const out = new Float32Array(bytes.length * 2);
    for (let i = 0; i < bytes.length; i++) {
      out[i * 2] = amp(bytes[i] >> 4);
      out[i * 2 + 1] = amp(bytes[i] & 0x0f);
    }
    return out;
  }
  const frames = Math.floor(bytes.length / BYTES_PER_FRAME);
  const out = new Float32Array(frames * SAMPLES_PER_FRAME);
  for (let c = 0; c < frames; c++) {
    const raw = new Array<number>(SAMPLES_PER_FRAME);
    for (let i = 0; i < BYTES_PER_FRAME; i++) {
      const b = bytes[c * BYTES_PER_FRAME + i];
      raw[i * 2] = b >> 4;
      raw[i * 2 + 1] = b & 0x0f;
    }
    for (let i = 0; i < SAMPLES_PER_FRAME; i++) out[c * SAMPLES_PER_FRAME + i] = amp(raw[(i + 1) % SAMPLES_PER_FRAME]);
  }
  return out;
}

// Write a name into a fixed-width field: uppercase ASCII, truncated to `size`, NUL-padded to the end.
export function writeName(writer: BitWriter, off: number, size: number, name: string): void {
  const up = name.toUpperCase();
  for (let i = 0; i < size; i++) {
    const c = i < up.length ? up.charCodeAt(i) : 0;
    writer.setU8(off + i, c >= 0x20 && c <= 0x7e ? c : 0);
  }
}

export class KitView {
  private readonly view: BitView;
  private readonly writer: BitWriter;
  readonly bank: number;
  private readonly base: number;

  // `rotate` = does this ROM's kit encoding apply the LSDj 9.2.0+ per-frame rightward rotation (threaded
  // from the ROM version by LsdjRom). Affects sampleData decode only.
  constructor(rom: Uint8Array, readonly index: number, private readonly rotate = true) {
    this.bank = KIT_LOOKUP[index];
    this.base = this.bank * BANK_SIZE;
    this.view = new BitView(rom);
    this.writer = new BitWriter(rom);
  }

  /** The i-th u16le offset-table entry as a GB address (0x4000-based). */
  private entry(i: number): number {
    return this.view.u16le(this.base + i * 2);
  }

  get valid(): boolean {
    return this.entry(0) === KIT_VALID_MAGIC;
  }
  get empty(): boolean {
    return this.entry(0) === KIT_EMPTY_MAGIC;
  }

  /** Number of populated samples: leading non-terminator offset entries, minus the start entry. */
  sampleCount(): number {
    if (!this.valid) return 0;
    let n = 0;
    while (n < KIT_OFFSET_ENTRIES) {
      const e = this.entry(n);
      if (e === 0 || e === KIT_EMPTY_MAGIC) break;
      n++;
    }
    return Math.max(0, n - 1);
  }

  name(): string {
    return readName(this.view, this.base + KIT_NAME_OFFSET, KIT_NAME_SIZE);
  }

  sampleName(i: number): string {
    return readName(this.view, this.base + KIT_SAMPLE_NAME_OFFSET + i * KIT_SAMPLE_NAME_SIZE, KIT_SAMPLE_NAME_SIZE);
  }

  /** The i-th sample's raw nibble bytes (the on-ROM stream, for lossless kit rebuilds). */
  rawSampleBytes(i: number): Uint8Array {
    if (i < 0 || i >= this.sampleCount()) return new Uint8Array(0);
    const start = this.entry(i) - 0x4000; // in-bank byte offset
    const end = this.entry(i + 1) - 0x4000;
    return this.view.slice(this.base + start, Math.max(0, end - start));
  }

  /** All populated samples as {name, raw nibble bytes} — the input to a kit rebuild (buildKitBank). */
  samplesRaw(): { name: string; bytes: Uint8Array }[] {
    const out: { name: string; bytes: Uint8Array }[] = [];
    for (let i = 0; i < this.sampleCount(); i++) out.push({ name: this.sampleName(i), bytes: this.rawSampleBytes(i) });
    return out;
  }

  /** The i-th sample's PCM in [-1, 1], decoded from its 4-bit nibbles: inverted (amp = 0xF - stored) and,
   *  for LSDj 9.2.0+ ROMs, un-rotating each 32-sample frame. Matches native SampleUtil.hpp. */
  sampleData(i: number): Float32Array {
    return decodeNibbles(this.rawSampleBytes(i), this.rotate);
  }

  /** Patch the 6-char kit name in place (uppercased, truncated, NUL-padded). */
  setName(name: string): void {
    writeName(this.writer, this.base + KIT_NAME_OFFSET, KIT_NAME_SIZE, name);
  }

  /** Patch the i-th 3-char sample name in place. */
  setSampleName(i: number, name: string): void {
    if (i < 0 || i >= KIT_MAX_SAMPLES) return;
    writeName(this.writer, this.base + KIT_SAMPLE_NAME_OFFSET + i * KIT_SAMPLE_NAME_SIZE, KIT_SAMPLE_NAME_SIZE, name);
  }

  /** Extract the whole kit to a plain object (names + decoded sample PCM). */
  toObject(): RomKit {
    const count = this.sampleCount();
    const samples: RomSample[] = [];
    for (let i = 0; i < count; i++) samples.push({ name: this.sampleName(i), pcm: this.sampleData(i) });
    return { index: this.index, bank: this.bank, valid: this.valid, empty: this.empty, name: this.name(), samples };
  }
}
