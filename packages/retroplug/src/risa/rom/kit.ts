// risa DPCM kit-bank codec (read side) + the metadata-mirror derivation + the DMC decoder. The 8 KB kit
// bank is produced natively (audioDriver.compileDmc); this reads one back (bankToModel), derives the
// resident-mirror rows from a bank's own bytes (deriveMetaFromBank — the model-free path from risa's
// rom_upgrade.js), and decodes a DPCM stream to audio (dpcmDecode) for round-trip verification. Ported
// from risa's tools/rom_patcher/src/{kit_bank_parser.js, kit_editor/decoder.js, rom_upgrade.js}.

import {
  KIT_NAME_OFFSET,
  KIT_NAME_SIZE,
  KIT_SAMPLE_NAMES,
  KIT_SAMPLE_NAME_LEN,
  KIT_INDEX_OFFSET,
  KIT_INDEX_ENTRY,
  KIT_SLOT_COUNT,
  KIT_MAGIC,
  KIT_MAGIC_OFFSET,
  KIT_SLOT_EMPTY,
  KIT_FLAG_LOOP,
  KIT_SAMPLE_REGION,
  SAMPLE_ALIGN,
  LENGTH_STEP,
} from "./constants";

/** One populated kit slot decoded from a bank. `dpcm` is the raw DMC bytes. */
export interface KitSlot {
  addr: number; // $4012 register (sample offset = addr * 64)
  lenReg: number; // $4013 register (byte length = lenReg * 16 + 1)
  rate: number; // PAL DPCM rate index 0..15
  loop: boolean;
  name: string; // 3-char sample name (trimmed)
  dpcm: Uint8Array;
}

/** A decoded kit bank: its name + 16 slots (null where empty). */
export interface KitModel {
  name: string;
  slots: (KitSlot | null)[];
}

function ascii(bytes: Uint8Array, off: number, len: number): string {
  let s = "";
  for (let i = 0; i < len; i++) {
    const b = bytes[off + i];
    if (b === 0) break;
    s += String.fromCharCode(b);
  }
  return s.replace(/\s+$/, "");
}

/** Parse an 8 KB kit bank into its name + populated slots (index table + sample names + DPCM bytes). */
export function bankToModel(bank: Uint8Array): KitModel {
  const slots: (KitSlot | null)[] = [];
  for (let slot = 0; slot < KIT_SLOT_COUNT; slot++) {
    const pos = KIT_INDEX_OFFSET + slot * KIT_INDEX_ENTRY;
    const addr = bank[pos];
    if (addr === KIT_SLOT_EMPTY) {
      slots.push(null);
      continue;
    }
    const lenReg = bank[pos + 1];
    const sampleOff = addr * SAMPLE_ALIGN;
    const byteLen = lenReg * LENGTH_STEP + 1;
    slots.push({
      addr,
      lenReg,
      rate: bank[pos + 2] & 0x0f,
      loop: (bank[pos + 3] & KIT_FLAG_LOOP) !== 0,
      name: ascii(bank, KIT_SAMPLE_NAMES + slot * KIT_SAMPLE_NAME_LEN, KIT_SAMPLE_NAME_LEN),
      dpcm: bank.slice(sampleOff, Math.min(sampleOff + byteLen, KIT_SAMPLE_REGION)),
    });
  }
  return { name: ascii(bank, KIT_NAME_OFFSET, KIT_NAME_SIZE), slots };
}

/** True if a bank is populated (the 0xA5 magic is present). */
export function isBankPopulated(bank: Uint8Array): boolean {
  return bank[KIT_MAGIC_OFFSET] === KIT_MAGIC;
}

/** Derive the three metadata-mirror rows for a kit slot from a bank's own bytes (no model needed) —
 *  risa's rom_upgrade.js updateRomKitMetadataFromBank. Used so a bank splice can update the mirror. */
export function deriveMetaFromBank(bank: Uint8Array): {
  nameBytes: Uint8Array;
  sampleNamesBytes: Uint8Array;
  slotPresentBytes: Uint8Array;
} {
  const populated = isBankPopulated(bank);
  const nameBytes = populated ? bank.slice(KIT_NAME_OFFSET, KIT_NAME_OFFSET + KIT_NAME_SIZE) : new Uint8Array(KIT_NAME_SIZE);
  const sampleNamesBytes = populated
    ? bank.slice(KIT_SAMPLE_NAMES, KIT_SAMPLE_NAMES + KIT_SLOT_COUNT * KIT_SAMPLE_NAME_LEN)
    : new Uint8Array(KIT_SLOT_COUNT * KIT_SAMPLE_NAME_LEN).fill(0x20);
  const slotPresentBytes = new Uint8Array(KIT_SLOT_COUNT);
  if (populated) {
    for (let slot = 0; slot < KIT_SLOT_COUNT; slot++) {
      slotPresentBytes[slot] = bank[KIT_INDEX_OFFSET + slot * KIT_INDEX_ENTRY] === KIT_SLOT_EMPTY ? 0 : 1;
    }
  }
  return { nameBytes, sampleNamesBytes, slotPresentBytes };
}

/** Decode a DPCM byte stream to float32 audio in ~[-1, 1] — the exact inverse of the native ±2 delta
 *  encoder (counter starts 64, ±2 with <127 / >0 guards, LSB-first), for round-trip verification. */
export function dpcmDecode(bytes: Uint8Array): Float32Array {
  const out = new Float32Array(bytes.length * 8);
  let counter = 64;
  for (let b = 0; b < bytes.length; b++) {
    const byte = bytes[b];
    for (let j = 0; j < 8; j++) {
      if (byte & (1 << j)) {
        if (counter < 127) counter += 2;
      } else {
        if (counter > 0) counter -= 2;
      }
      out[b * 8 + j] = (counter - 64) / 63.5;
    }
  }
  return out;
}
