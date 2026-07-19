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

/** One occupied saved-project slot: index + name. */
export interface SavProjectInfo {
  slot: number;
  name: string;
}

/** List the occupied saved-project slots WITHOUT decompressing any song — a cheap header-only scan (the
 *  block allocation table + name table), suitable for per-render menu use. Returns [] for a 32 KiB
 *  early-SRAM image or a non-LSDj / undersized buffer. */
export function listProjects(savBytes: Uint8Array): SavProjectInfo[] {
  if (savBytes.length < kSavSize) return [];
  if (savBytes[kInit] !== 0x6a /* 'j' */ || savBytes[kInit + 1] !== 0x6b /* 'k' */) return [];
  const seen = new Set<number>();
  const out: SavProjectInfo[] = [];
  for (let i = 0; i < kBlockCount; i++) {
    const p = savBytes[kAllocTable + i];
    if (p === kEmptyBlock || p >= kProjectCount || seen.has(p)) continue;
    seen.add(p);
    out.push({ slot: p, name: readName(savBytes, kProjectNames + p * kNameLen) });
  }
  return out.sort((a, b) => a.slot - b.slot);
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

// --- byte-level song placement + reads (song edits must NOT round-trip through the Song model — the model
// is only a lossless byte round-trip WITH a template, which stored projects don't get, so re-encoding
// silently corrupts ~300 bytes/song; also the model can't hold .lsdprj's 6-bit kit references) ----------

/** Decompress an occupied slot's stored song to its raw 0x8000 bytes (null if the slot is empty). */
export function decompressSlot(savBytes: Uint8Array, slot: number): Uint8Array | null {
  if (savBytes.length < kSavSize || slot < 0 || slot >= kProjectCount) return null;
  const blockArea = savBytes.subarray(kBlockArea, kBlockArea + kBlockCount * kBlockSize);
  for (let i = 0; i < kBlockCount; i++) {
    if (savBytes[kAllocTable + i] === slot) return decompressProject(blockArea, i);
  }
  return null;
}

/** A slot's 8-char name / version byte from the header (no decompression). */
export function savSongName(savBytes: Uint8Array, slot: number): string {
  return readName(savBytes, kProjectNames + slot * kNameLen);
}

/** The name of the currently-loaded (working) song — the working song is a copy of the slot at
 *  activeProjectIndex, so its name is that slot's. null when no project is active (0xff) or the sav is
 *  too small. A cheap header read (no decompression), for recent-list / title display. */
export function workingSongName(savBytes: Uint8Array): string | null {
  if (savBytes.length <= kActiveProj) return null;
  const idx = savBytes[kActiveProj];
  if (idx === 0xff || idx >= 32) return null;
  return savSongName(savBytes, idx) || null;
}
export function savSongVersion(savBytes: Uint8Array, slot: number): number {
  return savBytes[kProjectVers + slot];
}

/** Load a stored song into working memory (offset 0) + mark it the active project — LSDj's FILE-screen
 *  load, byte-exact (no model). Returns a new image, or null if the slot is empty. */
export function loadSongToWorking(savBytes: Uint8Array, slot: number): Uint8Array | null {
  const raw = decompressSlot(savBytes, slot);
  if (!raw) return null;
  const out = savBytes.slice();
  out.set(raw, kWorkingSong);
  out[kActiveProj] = slot;
  return out;
}

// --- byte-level song placement (for imports that must NOT round-trip through the Song model, e.g. .lsdprj
// whose 6-bit kit references the model can't represent) ------------------------------------------------

// Position of a block's next-block jump pointer (the XX in an `E0 XX` block-switch), or -1 if the block ends
// in EOF (`E0 FF`) / has no jump. Walks the RLE token stream so an 0xE0 inside data isn't mistaken for it.
function blockJumpPos(block: Uint8Array): number {
  let pos = 0;
  while (pos < block.length) {
    const b = block[pos];
    if (b === 0xc0 /* RLE */) {
      pos += block[pos + 1] === 0xc0 ? 2 : 3;
    } else if (b === 0xe0 /* SA */) {
      const a = block[pos + 1];
      if (a === 0xe0) pos += 2;
      else if (a === 0xf0 || a === 0xf1) pos += 3; // default wave/instrument stamp (+ count)
      else if (a === 0xff) return -1; // EOF
      else return pos + 1; // block jump — the XX byte
    } else {
      pos += 1;
    }
  }
  return -1;
}

/** First saved-project slot (0..31) not present in the block allocation table, or -1 when all 32 are used. */
export function freeSongSlot(savBytes: Uint8Array): number {
  if (savBytes.length < kSavSize) return -1;
  const used = new Set<number>();
  for (let i = 0; i < kBlockCount; i++) {
    const p = savBytes[kAllocTable + i];
    if (p < kProjectCount) used.add(p);
  }
  for (let s = 0; s < kProjectCount; s++) if (!used.has(s)) return s;
  return -1;
}

/** Clear a slot's blocks (alloc-table entries) — makes the slot empty. Returns a new image. */
export function freeSong(savBytes: Uint8Array, slot: number): Uint8Array {
  const out = savBytes.slice();
  for (let i = 0; i < kBlockCount; i++) if (out[kAllocTable + i] === slot) out[kAllocTable + i] = kEmptyBlock;
  return out;
}

/** Inject a raw 0x8000 song into `slot` at the byte level (compress → allocate free blocks → chain the
 *  block-jump pointers → stamp name/version/alloc), leaving the working song + other projects byte-identical.
 *  Returns a new image, or null if the sav is invalid or out of free blocks. Callers replacing an occupied
 *  slot should freeSong(slot) first. */
export function injectSong(savBytes: Uint8Array, slot: number, name: string, version: number, songBytes: Uint8Array): Uint8Array | null {
  if (savBytes.length < kSavSize || songBytes.length !== kSongBytes || slot < 0 || slot >= kProjectCount) return null;
  let comp;
  try {
    comp = compressProject(songBytes, 1);
  } catch {
    return null; // song doesn't fit in the block budget
  }
  const out = savBytes.slice();
  const free: number[] = [];
  for (let i = 0; i < kBlockCount; i++) if (out[kAllocTable + i] === kEmptyBlock) free.push(i);
  const blocks = comp.blockCount;
  if (free.length < blocks) return null;
  const alloc = free.slice(0, blocks);
  for (let j = 0; j < blocks; j++) {
    const block = comp.bytes.subarray(j * kBlockSize, (j + 1) * kBlockSize);
    const dst = kBlockArea + alloc[j] * kBlockSize;
    out.set(block, dst);
    if (j < blocks - 1) {
      const jp = blockJumpPos(block);
      if (jp < 0) return null; // a non-final block with no jump ⇒ malformed
      out[dst + jp] = (alloc[j + 1] + 1) & 0xff; // 1-based next allocated block
    }
    out[kAllocTable + alloc[j]] = slot;
  }
  for (let i = 0; i < kNameLen; i++) out[kProjectNames + slot * kNameLen + i] = i < name.length ? name.charCodeAt(i) & 0xff : 0;
  out[kProjectVers + slot] = version & 0xff;
  return out;
}
