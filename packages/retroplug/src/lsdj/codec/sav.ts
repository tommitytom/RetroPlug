// Codec for the full 128 KiB LSDj .sav image — the pure-TS port of SavCodec.cpp.
// Working-memory song (raw, at offset 0) + 512-byte header at 0x8000 (project
// names/versions, 'jk' magic, active-project index, 191-entry block allocation
// table) + the RLE-compressed stored-project archive. Also handles the 32 KiB
// early-SRAM image (working-song-only, no header/archive).
import { decodeSong, encodeSong } from "./song";
import { compressProject, decompressProject, kBlockCount, kBlockSize, kEmptyBlock } from "./rle";
import type { Sav, StoredProject } from "../model";

export const kSavSize = 0x20000; // 128 KiB

const kWorkingSong = 0;
const kSongBytes = 0x8000;
const kProjectNames = 0x8000; // [32][8]
const kProjectVers = 0x8100; // [32]
const kReserved = 0x8120; // [30]
const kInit = 0x813e; // 'jk'
const kActiveProj = 0x8140;
const kAllocTable = 0x8141; // [191]
const kBlockArea = 0x8200; // 191 * 0x200
const kNameLen = 8;
const kProjectCount = 32;

function readName(b: Uint8Array, off: number): string {
  let s = "";
  for (let i = 0; i < kNameLen; i++) {
    const c = b[off + i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s;
}

/** Decode a 128 KiB (or 32 KiB early-SRAM) sav image. Throws if malformed. */
export function decodeSav(savBytes: Uint8Array): Sav {
  // Early LSDj used a 32 KiB SRAM: the whole image is the working-memory song,
  // no header, no archive. (Best-effort; re-encoding produces a modern 128 KiB sav.)
  if (savBytes.length < kSavSize) {
    if (savBytes.length < kSongBytes) throw new Error("sav smaller than 0x8000 bytes");
    return {
      activeProjectIndex: 0xff,
      reserved: new Array(30).fill(0),
      workingSong: decodeSong(savBytes.subarray(kWorkingSong, kSongBytes)),
      projects: new Array(kProjectCount).fill(null),
    };
  }
  if (savBytes[kInit] !== 0x6a /* 'j' */ || savBytes[kInit + 1] !== 0x6b /* 'k' */)
    throw new Error("missing 'jk' SRAM init magic");

  const reserved: number[] = [];
  for (let i = 0; i < 30; i++) reserved.push(savBytes[kReserved + i]);

  const projects: (StoredProject | null)[] = new Array(kProjectCount).fill(null);
  const blockArea = savBytes.subarray(kBlockArea, kBlockArea + kBlockCount * kBlockSize);

  // Decompress each stored project: walk the block allocation table; the first
  // block carrying project index p is that project's entry point.
  for (let i = 0; i < kBlockCount; i++) {
    const p = savBytes[kAllocTable + i];
    if (p === kEmptyBlock || p >= kProjectCount) continue;
    if (projects[p]) continue;
    const songBytes = decompressProject(blockArea, i);
    projects[p] = {
      name: readName(savBytes, kProjectNames + p * kNameLen),
      version: savBytes[kProjectVers + p],
      song: decodeSong(songBytes),
    };
  }

  return {
    activeProjectIndex: savBytes[kActiveProj],
    reserved,
    workingSong: decodeSong(savBytes.subarray(kWorkingSong, kSongBytes)),
    projects,
  };
}

/**
 * Encode the model to a 128 KiB image. `template`, when 0x20000 bytes, seeds the
 * buffer so regions the model doesn't own pass through (pass the original for a
 * round-trip; omit for authoring).
 */
export function encodeSav(sav: Sav, template?: Uint8Array): Uint8Array {
  const out = new Uint8Array(kSavSize);
  if (template !== undefined && template.length >= kSavSize) out.set(template.subarray(0, kSavSize));

  // Working-memory song (pass the template's working song through so unmodeled
  // song regions stay byte-identical).
  {
    const tmpl = template !== undefined && template.length >= kSongBytes ? template.subarray(0, kSongBytes) : undefined;
    const ws = encodeSong(sav.workingSong, tmpl);
    out.set(ws, kWorkingSong);
  }

  // Header. Name/version tables for ABSENT projects pass through from the
  // template; only present projects are overwritten below. The block alloc table
  // is reset to the empty marker then filled.
  for (let i = 0; i < 30; i++) out[kReserved + i] = sav.reserved[i];
  out[kInit] = 0x6a; // 'j'
  out[kInit + 1] = 0x6b; // 'k'
  out[kActiveProj] = sav.activeProjectIndex;
  out.fill(kEmptyBlock, kAllocTable, kAllocTable + kBlockCount);

  // Compress present projects into the block area (sequential blocks, 1-based
  // like liblsdj compress_projects), filling names/versions and the alloc table.
  let currentBlock = 1; // 1-based
  for (let i = 0; i < kProjectCount; i++) {
    const proj = sav.projects[i];
    if (!proj) continue;

    const nameBytes = proj.name;
    for (let n = 0; n < Math.min(nameBytes.length, kNameLen); n++) out[kProjectNames + i * kNameLen + n] = nameBytes.charCodeAt(n) & 0xff;
    out[kProjectVers + i] = proj.version;

    const songBytes = encodeSong(proj.song);
    let comp;
    try {
      comp = compressProject(songBytes, currentBlock);
    } catch {
      continue; // song didn't fit; skip (won't happen for valid songs)
    }
    const dst = kBlockArea + (currentBlock - 1) * kBlockSize;
    out.set(comp.bytes, dst);
    for (let b = 0; b < comp.blockCount; b++) out[kAllocTable + (currentBlock - 1) + b] = i;
    currentBlock += comp.blockCount;
  }

  return out;
}
