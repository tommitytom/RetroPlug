// The `.lsdprj` project file — LSDj's current SD-card format: a `.lsdsng` (name[8] + version[1] + the
// RLE-compressed song blocks) with the raw 16 KB kit banks the song uses concatenated after it. Importing
// needs a base ROM to patch those kits into (dedupe against existing kits, add the missing ones to free
// slots, and remap the song's kit-instrument references) — see lsdpatch Document/LSDSavFile.java.
//
// This module works at the RAW 0x8000 song-byte level (not the Song model): our model reads kit indices as
// 5 bits, but imported kits routinely land in ROM slots >= 32 (6-bit), so the model would corrupt them.
import { compressProject, decompressSongStream, kBlockSize } from "./rle";
import { kInstrumentBytes, kInstrumentCount, kModernRegions } from "./regions";

const instrumentParams = kModernRegions.instrumentParams; // 0x3080, version-stable

const kNameLen = 8;
const kHeaderLen = kNameLen + 1; // name[8] + version[1]
const kKitBankSize = 0x4000; // 16 KiB
const kSongBytes = 0x8000;

// Kit instrument layout in the decompressed 0x8000 song (mirrors lsdpatch usedKits): type byte at +0 == 2,
// the two kit-bank references at +2 and +9, each a 6-bit index in the low bits.
const KIT_TYPE = 2;
const KIT_REF_OFFSETS = [2, 9] as const;
const KIT_MASK = 0x3f;

export interface Lsdprj {
  name: string;
  version: number;
  songBytes: Uint8Array; // the decompressed 0x8000 song (raw, for byte-level kit remapping)
  kitBanks: Uint8Array[]; // the trailing 16 KB kit banks, in file order (ascending used-kit order)
}

/** Decode a `.lsdprj` file: name/version, the decompressed song, and its trailing kit banks. Throws on a
 *  malformed file (bad header size, bad song, or a non-16 KB kit tail). */
export function decodeLsdprj(file: Uint8Array): Lsdprj {
  if (file.length < kHeaderLen + kBlockSize) throw new Error(`not a .lsdprj file: ${file.length} bytes`);
  let name = "";
  for (let i = 0; i < kNameLen; i++) {
    const c = file[i];
    if (c === 0) break;
    name += String.fromCharCode(c);
  }
  const version = file[kNameLen];
  const body = file.subarray(kHeaderLen);
  const { song, blocksUsed } = decompressSongStream(body);
  const kitStart = blocksUsed * kBlockSize;
  const kitTail = body.length - kitStart;
  if (kitTail < 0 || kitTail % kKitBankSize !== 0) throw new Error(`.lsdprj kit tail is not whole 16 KB banks (${kitTail})`);
  const kitBanks: Uint8Array[] = [];
  for (let off = kitStart; off < body.length; off += kKitBankSize) kitBanks.push(body.slice(off, off + kKitBankSize));
  return { name, version, songBytes: song, kitBanks };
}

/** Extract just kit bank `ordinal` from a `.lsdprj` (the role uses this at construct without decoding the
 *  song). Null if the ordinal is out of range / the file is malformed. */
export function lsdprjKitBank(file: Uint8Array, ordinal: number): Uint8Array | null {
  try {
    const banks = decodeLsdprj(file).kitBanks;
    return ordinal >= 0 && ordinal < banks.length ? banks[ordinal] : null;
  } catch {
    return null;
  }
}

/** The sorted-distinct kit-bank indices the song references (mirrors lsdpatch usedKits) — its order matches
 *  the `.lsdprj`'s trailing kit banks. */
export function usedKitIndices(songBytes: Uint8Array): number[] {
  const set = new Set<number>();
  for (let i = 0; i < kInstrumentCount; i++) {
    const base = instrumentParams + i * kInstrumentBytes;
    if (songBytes[base] !== KIT_TYPE) continue;
    for (const off of KIT_REF_OFFSETS) set.add(songBytes[base + off] & KIT_MASK);
  }
  return [...set].sort((a, b) => a - b);
}

/** Rewrite the song's kit-instrument references in place via `map` (old kit index -> new ROM kit slot),
 *  preserving the high 2 bits. Indices absent from the map are left unchanged. */
export function remapSongKits(songBytes: Uint8Array, map: Map<number, number>): void {
  for (let i = 0; i < kInstrumentCount; i++) {
    const base = instrumentParams + i * kInstrumentBytes;
    if (songBytes[base] !== KIT_TYPE) continue;
    for (const off of KIT_REF_OFFSETS) {
      const v = songBytes[base + off];
      const next = map.get(v & KIT_MASK);
      if (next != null) songBytes[base + off] = (v & ~KIT_MASK) | (next & KIT_MASK);
    }
  }
}

/** Serialize a song + kit banks to a `.lsdprj` file (name[8] + version[1] + song blocks + kit banks). Used
 *  by tests / a future export path. */
export function encodeLsdprj(prj: Lsdprj): Uint8Array {
  if (prj.songBytes.length !== kSongBytes) throw new Error("song must be 0x8000 bytes");
  const comp = compressProject(prj.songBytes, 1);
  const kitLen = prj.kitBanks.reduce((n, k) => n + k.length, 0);
  const out = new Uint8Array(kHeaderLen + comp.bytes.length + kitLen);
  for (let i = 0; i < Math.min((prj.name || "").length, kNameLen); i++) out[i] = prj.name.charCodeAt(i) & 0xff;
  out[kNameLen] = prj.version & 0xff;
  out.set(comp.bytes, kHeaderLen);
  let off = kHeaderLen + comp.bytes.length;
  for (const bank of prj.kitBanks) {
    out.set(bank, off);
    off += bank.length;
  }
  return out;
}
