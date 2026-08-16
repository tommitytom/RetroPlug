// The SMDJ4 battery image: superblock + a packed 32-entry directory + an RLE heap.
//
// Format reference: /workspaces/smsggdj/SAVEFORMAT.md, with tools/smdj4.js as the executable oracle
// (its own self-test prints ALL PASS). Every op here is BYTE-LEVEL over the image and returns a NEW
// image, matching the SongCatalog contract - the decoded-song model is deliberately never involved in
// song management, because a lossy round-trip through it would silently rewrite songs the user only
// asked to reorder.
//
//   $0000   32     superblock: "SMDJ4", version, entry count, 25 reserved
//   $0020   1024   directory: 32 entries x 32 B
//   $0420   ...    heap: song blobs (RLE, or store-raw when RLE would not shrink), grows upward
//
// Two placement rules a naive implementation gets wrong, both mirroring `rle_can_save` / `rle_compact`
// in the cart's src/rle.asm:
//
//   NO-STRADDLE. A 32 KB cart is two 16 KB SRAM banks and the ROM sets $FFFC per blob, so a blob may
//   never cross $4000 - the cart would decode across a bank switch it does not perform mid-stream.
//
//   THE CONFIG CAP. The OPTIONS block lives at $3F60 INSIDE bank 0's heap range, so a bank-0 blob may
//   not grow into it. The check reserves the FULL uncompressed 6,912 bytes rather than the blob's
//   actual packed size - conservative, and what the cart does, so an image built here stays saveable
//   on hardware. It also subsumes no-straddle: what fits under $3F60 cannot reach $4000.
import { rleDecompress, rlePack } from "./rle";

/** The decompressed song block: the same bytes the cart holds in work RAM. */
export const SMDJ4_BLOCK_LEN = 6912;

const MAGIC = [0x53, 0x4d, 0x44, 0x4a, 0x34]; // "SMDJ4"
const SUPER_LEN = 32;
const DIR_ENTRIES = 32;
const DIR_ENTRY_LEN = 32;
const DIR_OFF = SUPER_LEN;
const HEAP_OFF = SUPER_LEN + DIR_ENTRIES * DIR_ENTRY_LEN; // $0420 = 1056
const BANK = 0x4000;

// Superblock fields.
const SUPER_VERSION = 0x05;
const SUPER_COUNT = 0x06;
/** The currently-loaded slot, stored as SLOT + 1 so 0 means "none".
 *
 *  Not the raw slot number, and that is load-bearing: this byte lives in the superblock's reserved
 *  area, which every save written before it existed leaves as $00. A raw slot would make all of them
 *  claim slot 0 - and, once the cart honours this at boot, autoload it, breaking the "a first power-on
 *  should make sound" rule the ROM deliberately keeps (main.asm:238). With slot+1 an old save reads as
 *  "nothing loaded", which is exactly what it means. */
const SUPER_CUR_SLOT = 0x07;

// Directory entry fields.
const E_VALID = 0x00;
const E_RAW = 0x01;
const E_OFF = 0x02; // 2 B LE, relative to HEAP_OFF
const E_LEN = 0x04; // 2 B LE
const E_SUM = 0x06; // 2 B LE, over the DECOMPRESSED block
const E_ECHO = 0x08; // 8 B
const E_NAME = 0x10; // 8 B
const VALID = 0xa5;
const NAME_LEN = 8;
const ECHO_LEN = 8;

/** OPTIONS block offset: an 8 KB cart mirrors the window, so it lands lower. */
function configOffset(totalBytes: number): number {
  return totalBytes <= 0x2000 ? 0x1f60 : 0x3f60;
}
/** The longest OPTIONS form (v3, since v0.37). The loader accepts 7/8/10-byte blocks, so preserving
 *  the full 10 carries any of them across a song edit untouched. */
const CONFIG_LEN = 10;

const entryAt = (slot: number): number => DIR_OFF + slot * DIR_ENTRY_LEN;
const u16 = (b: Uint8Array, at: number): number => b[at] | (b[at + 1] << 8);
const setU16 = (b: Uint8Array, at: number, v: number): void => {
  b[at] = v & 0xff;
  b[at + 1] = (v >> 8) & 0xff;
};

/** The ROM's `sram_sum`: a 16-bit LE sum over the decompressed block. */
export function blockChecksum(block: Uint8Array): number {
  let s = 0;
  for (let i = 0; i < block.length; i++) s = (s + block[i]) & 0xffff;
  return s;
}

/** True when `bytes` is a readable SMDJ4 image. Magic + a sane entry count only: a per-entry checksum
 *  failure makes THAT song unreadable, not the whole cart, so it is not a validity question. */
export function isSmsggdjSav(bytes: Uint8Array): boolean {
  if (bytes.length < HEAP_OFF) return false;
  if (!MAGIC.every((m, i) => bytes[i] === m)) return false;
  const count = bytes[SUPER_COUNT];
  return count > 0 && count <= DIR_ENTRIES;
}

function entryCount(sav: Uint8Array): number {
  const n = sav[SUPER_COUNT];
  return n > 0 && n <= DIR_ENTRIES ? n : DIR_ENTRIES;
}

/** A directory entry's name, trimmed of the NUL/space padding the cart writes. */
function readName(sav: Uint8Array, slot: number): string {
  const e = entryAt(slot);
  let s = "";
  for (let i = 0; i < NAME_LEN; i++) {
    const c = sav[e + E_NAME + i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.replace(/\s+$/, "");
}

function writeName(sav: Uint8Array, slot: number, name: string): void {
  const e = entryAt(slot) + E_NAME;
  for (let i = 0; i < NAME_LEN; i++) sav[e + i] = i < name.length ? name.charCodeAt(i) & 0xff : 0;
}

/** The valid songs, in directory order. The directory is kept PACKED by the cart (valid entries are
 *  contiguous 0..count-1), so this stops at the first free entry rather than scanning past it - a hole
 *  would mean a corrupt image, and treating it as a gap to skip would let one song hide another. */
export function listSongs(sav: Uint8Array): { index: number; name: string }[] {
  if (!isSmsggdjSav(sav)) return [];
  const out: { index: number; name: string }[] = [];
  const n = entryCount(sav);
  for (let slot = 0; slot < n; slot++) {
    if (sav[entryAt(slot) + E_VALID] !== VALID) break;
    out.push({ index: slot, name: readName(sav, slot) });
  }
  return out;
}

/** The stored blob for a slot (still packed), or null when the slot is free / points out of range. */
function readBlob(sav: Uint8Array, slot: number): { raw: boolean; bytes: Uint8Array; sum: number } | null {
  if (slot < 0 || slot >= entryCount(sav)) return null;
  const e = entryAt(slot);
  if (sav[e + E_VALID] !== VALID) return null;
  const off = HEAP_OFF + u16(sav, e + E_OFF);
  const len = u16(sav, e + E_LEN);
  if (len === 0 || off + len > sav.length) return null;
  return { raw: sav[e + E_RAW] === 1, bytes: sav.subarray(off, off + len), sum: u16(sav, e + E_SUM) };
}

/** A slot's decoded 6,912-byte song block, or null when the slot is free, malformed, or fails its
 *  stored checksum - the same three refusals the cart makes ("a song entry loads only when
 *  valid == $A5 and the stored checksum matches"). */
export function readSongBlock(sav: Uint8Array, slot: number): Uint8Array | null {
  const blob = readBlob(sav, slot);
  if (!blob) return null;
  const block = blob.raw
    ? blob.bytes.length === SMDJ4_BLOCK_LEN
      ? blob.bytes.slice()
      : null
    : rleDecompress(blob.bytes, SMDJ4_BLOCK_LEN);
  if (!block) return null;
  return blockChecksum(block) === blob.sum ? block : null;
}

/** The currently-loaded slot, or -1 when the save names none (including every save written before the
 *  field existed). See SUPER_CUR_SLOT. */
export function curSlot(sav: Uint8Array): number {
  if (!isSmsggdjSav(sav)) return -1;
  const v = sav[SUPER_CUR_SLOT];
  if (v === 0) return -1;
  const slot = v - 1;
  return slot < entryCount(sav) && sav[entryAt(slot) + E_VALID] === VALID ? slot : -1;
}

/** Name the currently-loaded slot (-1 clears it). Returns a NEW image, or null when the slot is not a
 *  valid song - the cart would boot to a blank song and the save would be lying about what is loaded. */
export function setCurSlot(sav: Uint8Array, slot: number): Uint8Array | null {
  if (!isSmsggdjSav(sav)) return null;
  if (slot >= 0 && (slot >= entryCount(sav) || sav[entryAt(slot) + E_VALID] !== VALID)) return null;
  const out = sav.slice();
  out[SUPER_CUR_SLOT] = slot < 0 ? 0 : slot + 1;
  return out;
}

/** One song, detached from any image - what the heap ops move around. */
interface DetachedSong {
  block: Uint8Array; // decoded, 6912
  name: Uint8Array; // 8 raw bytes, carried verbatim
  echo: Uint8Array; // 8 raw bytes, carried verbatim
}

function detach(sav: Uint8Array, slot: number): DetachedSong | null {
  const block = readSongBlock(sav, slot);
  if (!block) return null;
  const e = entryAt(slot);
  return {
    block,
    name: sav.subarray(e + E_NAME, e + E_NAME + NAME_LEN).slice(),
    echo: sav.subarray(e + E_ECHO, e + E_ECHO + ECHO_LEN).slice(),
  };
}

/** Rebuild an image from an ordered song list: superblock + packed directory + a freshly laid-out heap.
 *
 *  This IS the compaction the cart's `rle_compact` performs - surviving blobs slid down in order to
 *  close the gap, including across the bank boundary when room opens below - expressed as "place them
 *  all again from the base" rather than as an in-place slide. The result is identical because the
 *  directory is packed and placement is a pure function of the preceding blobs, and it cannot leave a
 *  stranded mid-heap hole by construction.
 *
 *  Preserves everything outside the structure byte-for-byte (the OPTIONS block at $3F60 above all), so
 *  a song edit never disturbs the machine config sitting in the middle of the heap range.
 *
 *  Returns null when the songs do not fit - the caller reports SRAM FULL rather than silently dropping
 *  the tail. */
function rebuild(template: Uint8Array, songs: DetachedSong[], cur: number): Uint8Array | null {
  const total = template.length;
  const cfg = configOffset(total);
  const out = template.slice();

  // The OPTIONS block sits at $3F60, INSIDE bank 0's heap range - so it has to be lifted out before the
  // heap is cleared and put back after. Clearing the heap wholesale and restoring it is the one shape
  // that cannot leave a stale blob behind OR eat the machine config; carving the wipe into ranges
  // around it looked simpler and got the 32 KB case wrong (the tail wipe ran straight through $3F60).
  const savedConfig = out.subarray(cfg, Math.min(cfg + CONFIG_LEN, total)).slice();

  MAGIC.forEach((m, i) => (out[i] = m));
  out[SUPER_VERSION] = template[SUPER_VERSION] || 1;
  out[SUPER_COUNT] = DIR_ENTRIES;
  out.fill(0, DIR_OFF, DIR_OFF + DIR_ENTRIES * DIR_ENTRY_LEN);
  // Wipe the whole heap, not just the freed tail: a deleted song's bytes should not linger in a file
  // the user believes they cleaned, and it makes a `.sav` diff mean something.
  out.fill(0, HEAP_OFF, total);

  let heapEnd = HEAP_OFF;
  for (let slot = 0; slot < songs.length; slot++) {
    if (slot >= DIR_ENTRIES) return null;
    const s = songs[slot];

    // The config cap, reserving the worst case exactly as the cart does. Bank 1 has no config in it,
    // hence the `heapEnd < BANK` guard - once we are past the boundary the cap no longer applies.
    if (heapEnd < BANK && heapEnd + SMDJ4_BLOCK_LEN > cfg) {
      if (total > BANK) heapEnd = BANK; // 32 KB: bump this blob to bank 1
      else return null; // single-bank cart: nothing below the config can hold it
    }
    const { raw, bytes } = rlePack(s.block);
    if (heapEnd + bytes.length > total) return null;

    const e = entryAt(slot);
    out[e + E_VALID] = VALID;
    out[e + E_RAW] = raw ? 1 : 0;
    setU16(out, e + E_OFF, heapEnd - HEAP_OFF);
    setU16(out, e + E_LEN, bytes.length);
    setU16(out, e + E_SUM, blockChecksum(s.block));
    out.set(s.echo, e + E_ECHO);
    out.set(s.name, e + E_NAME);
    out.set(bytes, heapEnd);
    heapEnd += bytes.length;
  }

  out.set(savedConfig, cfg); // the machine config survives every song edit
  out[SUPER_CUR_SLOT] = cur >= 0 && cur < songs.length ? cur + 1 : 0;
  return out;
}

/** Every song the directory claims, detached - or NULL when any one of them will not decode.
 *
 *  Refusing the whole image is the entire point, and the failure it prevents is severe. `listSongs`
 *  already stops at a structural hole (a cleared valid byte), so every entry it hands back is a song the
 *  directory says exists. If one of those then fails its checksum or its RLE stream, that is PAYLOAD
 *  corruption - it condemns that one song and says nothing whatever about the entries after it.
 *
 *  So this must not do what a `break` would: detach the prefix and let `rebuild` lay out an image from
 *  it. That image is perfectly well-formed, `rebuild` returns it happily, and the caller writes it
 *  straight over the user's `.sav` - turning one flipped checksum byte into the silent deletion of every
 *  song stored after it, during an edit aimed at a completely different slot. One bad byte may cost the
 *  user that one song; it may never cost them the rest of the cart. */
function detachAll(sav: Uint8Array): DetachedSong[] | null {
  const out: DetachedSong[] = [];
  for (const s of listSongs(sav)) {
    const d = detach(sav, s.index);
    if (!d) return null;
    out.push(d);
  }
  return out;
}

/** Delete the song at `slot`, compacting the heap and packing the directory down. Returns a NEW image,
 *  or null on an invalid slot. The currently-loaded marker follows the song it names: it shifts down
 *  with the survivors, and clears if the deleted song WAS the loaded one. */
export function deleteSong(sav: Uint8Array, slot: number): Uint8Array | null {
  const songs = detachAll(sav);
  if (!songs || slot < 0 || slot >= songs.length) return null;
  const cur = curSlot(sav);
  const nextCur = cur < 0 || cur === slot ? -1 : cur > slot ? cur - 1 : cur;
  songs.splice(slot, 1);
  return rebuild(sav, songs, nextCur);
}

/** Move the song at list position `from` to position `to`. Returns a NEW image, or null on an
 *  out-of-range or no-op move. The loaded marker tracks the MOVED song, not the position. */
export function reorderSongs(sav: Uint8Array, from: number, to: number): Uint8Array | null {
  const songs = detachAll(sav);
  if (!songs || from === to || from < 0 || to < 0 || from >= songs.length || to >= songs.length) return null;
  const cur = curSlot(sav);
  const [moved] = songs.splice(from, 1);
  songs.splice(to, 0, moved);
  let nextCur = cur;
  if (cur >= 0) {
    if (cur === from) nextCur = to;
    else if (from < cur && cur <= to) nextCur = cur - 1;
    else if (to <= cur && cur < from) nextCur = cur + 1;
  }
  return rebuild(sav, songs, nextCur);
}

/** Append `source`'s songs at `indices` to `target`. Returns a NEW image, or null when nothing could be
 *  imported (no valid source songs, or they do not fit). Byte-exact: each song's block, name and echo
 *  settings travel together, since name and echo live in the DIRECTORY ENTRY rather than the blob. */
export function importSongs(target: Uint8Array, source: Uint8Array, indices: number[]): Uint8Array | null {
  if (!isSmsggdjSav(target) || !isSmsggdjSav(source)) return null;
  const songs = detachAll(target);
  if (!songs) return null; // the TARGET is corrupt: importing would rebuild it minus the unreadable tail
  // The SOURCE is a different matter - it is someone else's file, the user ticked specific songs in it,
  // and a bad one there costs them nothing they own. Skip those and import the rest; the caller compares
  // counts and reports an incomplete import.
  const add = indices.map((i) => detach(source, i)).filter((s): s is DetachedSong => s !== null);
  if (!add.length) return null;
  return rebuild(target, [...songs, ...add], curSlot(target));
}

/** Append one song. Returns a NEW image, or null when it does not fit / the block is the wrong size. */
export function addSong(sav: Uint8Array, block: Uint8Array, name: string): Uint8Array | null {
  if (!isSmsggdjSav(sav) || block.length !== SMDJ4_BLOCK_LEN) return null;
  const songs = detachAll(sav);
  if (!songs) return null;
  const nameBytes = new Uint8Array(NAME_LEN);
  for (let i = 0; i < NAME_LEN && i < name.length; i++) nameBytes[i] = name.charCodeAt(i) & 0xff;
  songs.push({ block: block.slice(), name: nameBytes, echo: new Uint8Array(ECHO_LEN) });
  return rebuild(sav, songs, curSlot(sav));
}

/** Rename the song at `slot` in place - no heap movement, so no re-layout. */
export function renameSong(sav: Uint8Array, slot: number, name: string): Uint8Array | null {
  if (!isSmsggdjSav(sav) || slot < 0 || slot >= entryCount(sav)) return null;
  if (sav[entryAt(slot) + E_VALID] !== VALID) return null;
  const out = sav.slice();
  writeName(out, slot, name);
  return out;
}

/** Build an image from scratch - used by tests and by the CLI's song-seed path. */
export function buildSav(
  // `echo` is optional and defaults to all-zero (echo off), which is what a song saved by a cart with
  // echo disabled carries. It belongs on the SONG rather than on the image because SMDJ4 stores it per
  // directory entry, alongside the name.
  songs: { block: Uint8Array; name: string; echo?: Uint8Array }[],
  cartBytes = 32 * 1024,
  config?: Uint8Array,
): Uint8Array | null {
  const sav = new Uint8Array(cartBytes);
  MAGIC.forEach((m, i) => (sav[i] = m));
  sav[SUPER_VERSION] = 1;
  sav[SUPER_COUNT] = DIR_ENTRIES;
  if (config) sav.set(config.subarray(0, Math.min(config.length, 10)), configOffset(cartBytes));
  const detached = songs.map((s) => {
    const nameBytes = new Uint8Array(NAME_LEN);
    for (let i = 0; i < NAME_LEN && i < s.name.length; i++) nameBytes[i] = s.name.charCodeAt(i) & 0xff;
    const echoBytes = new Uint8Array(ECHO_LEN);
    if (s.echo) echoBytes.set(s.echo.subarray(0, ECHO_LEN));
    return { block: s.block, name: nameBytes, echo: echoBytes };
  });
  return rebuild(sav, detached, -1);
}

// --- the `.smdj4` single-song file ---------------------------------------------
//
// The interchange unit tools/savetool.html exports and accepts: a 16-byte header carrying the magic,
// the block checksum and the echo settings, followed by the verbatim 6,912-byte block. NOT RLE - a
// single song on disk has no space pressure, and staying uncompressed means the file is the same thing
// the cart holds in work RAM.

/** `.smdj4` header length; the block starts here. */
export const SMDJ4_SONG_HEADER_LEN = 16;
/** A whole `.smdj4` file. */
export const SMDJ4_SONG_FILE_LEN = SMDJ4_SONG_HEADER_LEN + SMDJ4_BLOCK_LEN;

/** Wrap a block as a `.smdj4` file, byte-compatible with tools/smdj4.js `wrapSmdj4`. */
export function wrapSong(block: Uint8Array, echo?: Uint8Array): Uint8Array | null {
  if (block.length !== SMDJ4_BLOCK_LEN) return null;
  const out = new Uint8Array(SMDJ4_SONG_FILE_LEN);
  MAGIC.forEach((m, i) => (out[i] = m));
  setU16(out, 5, blockChecksum(block));
  if (echo) out.set(echo.subarray(0, ECHO_LEN), 7);
  out.set(block, SMDJ4_SONG_HEADER_LEN);
  return out;
}

/** Unwrap a `.smdj4` file. Null unless the magic, the length AND the stored checksum all agree - the
 *  same three refusals the cart makes on a directory entry, applied to a file the user picked. */
export function unwrapSong(bytes: Uint8Array): { block: Uint8Array; echo: Uint8Array } | null {
  if (bytes.length !== SMDJ4_SONG_FILE_LEN) return null;
  if (!MAGIC.every((m, i) => bytes[i] === m)) return null;
  const block = bytes.subarray(SMDJ4_SONG_HEADER_LEN).slice();
  if (blockChecksum(block) !== u16(bytes, 5)) return null;
  return { block, echo: bytes.subarray(7, 7 + ECHO_LEN).slice() };
}

/** A slot's STORED block checksum, straight from its directory entry - no decode. Null when the slot is
 *  free or out of range. Lets a caller ask "is this block already saved somewhere?" for the price of a
 *  16-bit read per slot, decoding only the one entry whose sum matches. */
export function storedChecksum(sav: Uint8Array, slot: number): number | null {
  if (!isSmsggdjSav(sav) || slot < 0 || slot >= entryCount(sav)) return null;
  const e = entryAt(slot);
  return sav[e + E_VALID] === VALID ? u16(sav, e + E_SUM) : null;
}

/** Does `block` already exist, byte for byte, as a saved song? The question behind "would this edit lose
 *  work" for a cart whose working song is work RAM: if the live block is one of the saved ones, the reboot
 *  costs nothing. Checksums narrow it to a candidate, and only that candidate is decoded - a false match
 *  on a 16-bit sum is possible, so the full compare is what decides. */
export function isSongSaved(sav: Uint8Array, block: Uint8Array): boolean {
  if (block.length !== SMDJ4_BLOCK_LEN) return false;
  const want = blockChecksum(block);
  for (const s of listSongs(sav)) {
    if (storedChecksum(sav, s.index) !== want) continue;
    const saved = readSongBlock(sav, s.index);
    if (saved && saved.every((b, i) => b === block[i])) return true;
  }
  return false;
}

// --- fields WITHIN the song block, for a host-side load ------------------------
// Offsets into the 6,912-byte block (SAVEFORMAT.md's table). They are block-relative and therefore a
// property of the FORMAT, not of a build - unlike the work-RAM variables, which move with the linker.

const SONG_OFF = 0x1300; // 128 rows x 4 chain numbers, $FF = empty
const SONG_LEN = 512;
const GROOVE_OFF = 0x1a00; // 16 grooves x 16 tick bytes
const GROOVE_STRIDE = 16;

/** The song's length in ROWS, by the cart's own rule (`load_rebase`, engine.asm:2282-2308): scan the
 *  grid backwards for the last byte that is not $FF, then `ceil(bytes / 4)`, minimum 1. A song of
 *  nothing but empty rows is one row long, not zero - a zero would stall the sequencer.
 *
 *  Needed because the engine caches this in `eng_len`: a song loaded UNDER a running transport without
 *  it keeps wrapping at the previous song's length, which is permanent, not a glitch that settles. */
export function songLengthRows(block: Uint8Array): number {
  let remaining = SONG_LEN;
  for (let i = SONG_OFF + SONG_LEN - 1; remaining > 0; i--, remaining--) {
    if (block[i] !== 0xff) break;
  }
  return Math.max(1, (remaining + 3) >> 2);
}

/** True when the groove at `sel` is empty, i.e. its first tick is 0. The cart falls back to groove 0 in
 *  that case, because an empty groove gives the clock nothing to advance on. */
export function isGrooveEmpty(block: Uint8Array, sel: number): boolean {
  const at = GROOVE_OFF + sel * GROOVE_STRIDE;
  return at + 1 > block.length || block[at] === 0;
}

/** `echo_sanitize` (engine.asm:982-1011), applied to a directory entry's 8 echo bytes before they are
 *  written into a running cart. The cart runs this after its OWN load, so a host-side load that skipped
 *  it could leave out-of-range values a corrupt or foreign save carried - mode 3+, a zero delay tap -
 *  live in the engine. Pure, and returns a NEW array. */
export function sanitizeEcho(echo: Uint8Array): Uint8Array {
  const out = echo.slice();
  if (out[0] >= 3) out[0] = 0; // mode: 0 off, 1 = T2, 2 = T2+T3
  for (const tap of [1, 2]) out[tap] = out[tap] === 0 ? 1 : out[tap] >= 16 ? 15 : out[tap]; // 1..15 rows
  out[3] &= 0x0f; // red1
  out[4] &= 0x0f; // red2
  out[5] &= 1; // stereo
  return out; // tsp1/tsp2 are signed semitones - every byte is a legal value
}

/** A slot's RAW 8 name bytes, padding and all. `listSongs` trims for display; a host-side load has to
 *  write the cart's `song_name` verbatim, because trimming would leave stale characters behind it. */
export function readSongName(sav: Uint8Array, slot: number): Uint8Array | null {
  if (!isSmsggdjSav(sav) || slot < 0 || slot >= entryCount(sav)) return null;
  const e = entryAt(slot);
  if (sav[e + E_VALID] !== VALID) return null;
  return sav.subarray(e + E_NAME, e + E_NAME + NAME_LEN).slice();
}

/** A slot's echo settings (mode, taps, reductions, stereo, transposes) - they live in the DIRECTORY
 *  entry rather than the block, so an export has to fetch them separately to travel with the song. */
export function readSongEcho(sav: Uint8Array, slot: number): Uint8Array | null {
  if (!isSmsggdjSav(sav) || slot < 0 || slot >= entryCount(sav)) return null;
  const e = entryAt(slot);
  if (sav[e + E_VALID] !== VALID) return null;
  return sav.subarray(e + E_ECHO, e + E_ECHO + ECHO_LEN).slice();
}

/** Overwrite one slot's song, keeping its NAME (the user replaced the music in a named slot, not the
 *  slot itself). Re-lays the heap, since the new block may pack to a different size. */
export function replaceSong(sav: Uint8Array, slot: number, block: Uint8Array, echo?: Uint8Array): Uint8Array | null {
  if (block.length !== SMDJ4_BLOCK_LEN) return null;
  const songs = detachAll(sav);
  if (!songs || slot < 0 || slot >= songs.length) return null;
  songs[slot] = { block: block.slice(), name: songs[slot].name, echo: echo ? echo.slice() : songs[slot].echo };
  return rebuild(sav, songs, curSlot(sav));
}
