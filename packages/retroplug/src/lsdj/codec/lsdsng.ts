// The `.lsdsng` single-song file format — LSDj's classic song-only export (deprecated in favour of
// `.lsdprj`, which additionally bundles kit banks, but still universally supported). Layout:
//   name[8] (NUL-padded) + version[1] + N*512 RLE-compressed song blocks
// i.e. exactly what `compressProject(encodeSong(song), 1)` produces, prefixed by the name + version. The
// block-jump numbers a standalone `.lsdsng` uses count from block 0 (unlike the sav's shared block area),
// which `compressProject(song, 1)` already emits self-consistently, so `decompressProject(body, 0)` reads
// it straight back. Verified against real lsdpatch-exported files (../resources/retro/*.lsdsng).
import { decodeSong, encodeSong } from "./song";
import { compressProject, decompressSongStream, kBlockSize } from "./rle";
import type { StoredProject } from "../model";

const kNameLen = 8;
const kHeaderLen = kNameLen + 1; // name[8] + version[1]

/** Decode a `.lsdsng` file into a StoredProject. Throws on a malformed file (bad size / bad song). */
export function decodeLsdsng(file: Uint8Array): StoredProject {
  if (file.length < kHeaderLen + kBlockSize || (file.length - kHeaderLen) % kBlockSize !== 0)
    throw new Error(`not a .lsdsng file: ${file.length} bytes`);
  let name = "";
  for (let i = 0; i < kNameLen; i++) {
    const c = file[i];
    if (c === 0) break;
    name += String.fromCharCode(c);
  }
  const version = file[kNameLen];
  const { song: songBytes } = decompressSongStream(file.subarray(kHeaderLen));
  return { name, version, song: decodeSong(songBytes) };
}

/** Serialize a StoredProject to a `.lsdsng` file (name[8] + version[1] + compressed blocks). */
export function encodeLsdsng(project: StoredProject): Uint8Array {
  const comp = compressProject(encodeSong(project.song), 1);
  const out = new Uint8Array(kHeaderLen + comp.bytes.length);
  const name = project.name || "";
  for (let i = 0; i < Math.min(name.length, kNameLen); i++) out[i] = name.charCodeAt(i) & 0xff;
  out[kNameLen] = project.version & 0xff;
  out.set(comp.bytes, kHeaderLen);
  return out;
}

/** Decode a `.lsdsng` to its name/version + RAW 0x8000 song bytes (no Song model — for byte-exact import,
 *  which the model can't do without a template). Throws on a malformed file. */
export function decodeLsdsngRaw(file: Uint8Array): { name: string; version: number; songBytes: Uint8Array } {
  if (file.length < kHeaderLen + kBlockSize || (file.length - kHeaderLen) % kBlockSize !== 0)
    throw new Error(`not a .lsdsng file: ${file.length} bytes`);
  let name = "";
  for (let i = 0; i < kNameLen; i++) {
    const c = file[i];
    if (c === 0) break;
    name += String.fromCharCode(c);
  }
  return { name, version: file[kNameLen], songBytes: decompressSongStream(file.subarray(kHeaderLen)).song };
}

/** Serialize name/version + RAW 0x8000 song bytes to a `.lsdsng` (byte-exact — compression is lossless). */
export function encodeLsdsngRaw(name: string, version: number, songBytes: Uint8Array): Uint8Array {
  const comp = compressProject(songBytes, 1);
  const out = new Uint8Array(kHeaderLen + comp.bytes.length);
  for (let i = 0; i < Math.min((name || "").length, kNameLen); i++) out[i] = name.charCodeAt(i) & 0xff;
  out[kNameLen] = version & 0xff;
  out.set(comp.bytes, kHeaderLen);
  return out;
}
