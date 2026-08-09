// The SMDJ4 battery image: directory, heap placement, compaction, and the currently-loaded slot.
//
// Placement was certified byte-identical to /workspaces/smsggdj/tools/smdj4.js `buildSav` during
// authoring - 1/2/3/4 songs, sparse and store-raw, the bank bump, both SRAM-FULL cases, and 8/16/32 KB
// carts - and the oracle's `readSav` reads images this module writes. What is pinned here is the
// behaviour that certification cannot cover: the incremental ops (delete / reorder / import / add),
// which the oracle has no equivalent of, and the invariants a rebuild is easy to break.
import { test, expect } from "../../testing/harness";
import {
  SMDJ4_BLOCK_LEN,
  isSmsggdjSav,
  listSongs,
  readSongBlock,
  blockChecksum,
  curSlot,
  setCurSlot,
  deleteSong,
  reorderSongs,
  importSongs,
  addSong,
  renameSong,
  buildSav,
} from "../../src/smsggdj/codec/sav";

const CART = 32 * 1024;
const CFG_OFF = 0x3f60;

/** A compressible song, distinguishable by `tag`. */
const sparse = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
/** An incompressible song - forces the store-raw floor, so it costs the full 6,912 bytes of heap. */
const dense = (seed: number): Uint8Array => {
  let s = seed;
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i++) {
    s = (s * 1103515245 + 12345) & 0x7fffffff;
    b[i] = (s >>> 16) & 0xff;
  }
  return b;
};
const names = (sav: Uint8Array): string[] => listSongs(sav).map((s) => s.name);

test("a built image is readable, listed, and round-trips every block", () => {
  const sav = buildSav([
    { block: sparse(1), name: "ONE" },
    { block: sparse(2), name: "TWO" },
  ], CART)!;
  expect(isSmsggdjSav(sav)).toBe(true);
  expect(sav.length).toBe(CART);
  expect(names(sav)).toEqual(["ONE", "TWO"]);
  expect(readSongBlock(sav, 0)).toEqual(sparse(1));
  expect(readSongBlock(sav, 1)).toEqual(sparse(2));
  expect(readSongBlock(sav, 2)).toBe(null); // free slot
});

test("a slot whose checksum does not match is refused, not loaded", () => {
  // The cart's own rule: "A song entry loads only when valid == $A5 and the stored checksum matches".
  // Returning a corrupt block instead would hand the user a mangled song that looks loadable.
  const sav = buildSav([{ block: sparse(1), name: "ONE" }], CART)!;
  const e = 32; // directory entry 0
  const corrupt = sav.slice();
  corrupt[e + 6] ^= 0xff; // stored checksum, low byte
  expect(readSongBlock(corrupt, 0)).toBe(null);
  expect(listSongs(corrupt).length).toBe(1); // still LISTED - one bad song is not a broken cart
});

test("a non-SMDJ4 buffer is rejected rather than parsed as an empty cart", () => {
  expect(isSmsggdjSav(new Uint8Array(CART))).toBe(false);
  expect(isSmsggdjSav(new Uint8Array(8))).toBe(false); // too short for a directory
  expect(listSongs(new Uint8Array(CART))).toEqual([]);
});

// --- the currently-loaded slot -------------------------------------------------

test("curSlot is stored as slot+1, so every save written before it existed reads as 'none'", () => {
  // The whole reason for the +1. The byte lives in the superblock's reserved area, which older saves
  // leave as $00; a raw slot number would make all of them claim - and, once the cart honours this at
  // boot, autoload - slot 0.
  const sav = buildSav([{ block: sparse(1), name: "ONE" }, { block: sparse(2), name: "TWO" }], CART)!;
  expect(curSlot(sav)).toBe(-1); // fresh image names nothing
  expect(sav[7]).toBe(0);

  const one = setCurSlot(sav, 1)!;
  expect(one[7]).toBe(2); // slot + 1, on the wire
  expect(curSlot(one)).toBe(1);

  expect(curSlot(setCurSlot(one, -1)!)).toBe(-1); // cleared
});

test("curSlot naming a free or out-of-range slot reads as none, and cannot be set", () => {
  const sav = buildSav([{ block: sparse(1), name: "ONE" }], CART)!;
  expect(setCurSlot(sav, 5)).toBe(null); // no such song
  expect(setCurSlot(sav, 1)).toBe(null); // free slot
  const forged = sav.slice();
  forged[7] = 9; // a save claiming slot 8, which does not exist
  expect(curSlot(forged)).toBe(-1); // read defensively, not trusted
});

// --- incremental ops -----------------------------------------------------------

test("delete compacts the heap, packs the directory, and moves the loaded marker with its song", () => {
  const sav = setCurSlot(
    buildSav([
      { block: sparse(1), name: "ONE" },
      { block: sparse(2), name: "TWO" },
      { block: sparse(3), name: "THREE" },
    ], CART)!,
    2,
  )!;
  const out = deleteSong(sav, 0)!;
  expect(names(out)).toEqual(["TWO", "THREE"]); // packed down, no hole
  expect(readSongBlock(out, 0)).toEqual(sparse(2)); // ...and the bytes followed the names
  expect(readSongBlock(out, 1)).toEqual(sparse(3));
  expect(curSlot(out)).toBe(1); // the loaded song was THREE; it is now slot 1, not still 2

  // Deleting the loaded song clears the marker rather than leaving it pointing at a stranger.
  expect(curSlot(deleteSong(out, 1)!)).toBe(-1);
  expect(deleteSong(sav, 7)).toBe(null);
});

test("delete frees real heap space, so a deleted store-raw song's room is reusable", () => {
  // The point of compaction. Deleting the FIRST song has to slide the second down from $1F20 to $0420,
  // or the freed room stays stranded mid-heap and the replacement lands somewhere else entirely.
  const two = buildSav([{ block: dense(1), name: "D1" }, { block: dense(2), name: "D2" }], CART)!;
  const afterDelete = deleteSong(two, 0)!;
  const refilled = addSong(afterDelete, dense(3), "D3");
  expect(refilled != null).toBeTruthy();
  expect(names(refilled!)).toEqual(["D2", "D3"]);
  expect(readSongBlock(refilled!, 0)).toEqual(dense(2)); // the survivor moved intact
});

test("reorder moves the song, and the loaded marker tracks the song rather than the position", () => {
  const sav = setCurSlot(
    buildSav([
      { block: sparse(1), name: "A" },
      { block: sparse(2), name: "B" },
      { block: sparse(3), name: "C" },
    ], CART)!,
    0,
  )!;
  const out = reorderSongs(sav, 0, 2)!;
  expect(names(out)).toEqual(["B", "C", "A"]);
  expect(readSongBlock(out, 2)).toEqual(sparse(1));
  expect(curSlot(out)).toBe(2); // A moved to the end; the marker went with it

  // A song the move steps over shifts by one.
  const shifted = reorderSongs(setCurSlot(sav, 1)!, 0, 2)!;
  expect(curSlot(shifted)).toBe(0); // B was slot 1, now slot 0

  expect(reorderSongs(sav, 1, 1)).toBe(null); // no-op
  expect(reorderSongs(sav, 0, 9)).toBe(null);
});

test("import appends byte-exact songs, carrying the name from the source entry", () => {
  const target = buildSav([{ block: sparse(1), name: "MINE" }], CART)!;
  const source = buildSav([
    { block: sparse(7), name: "THEIRS1" },
    { block: sparse(8), name: "THEIRS2" },
  ], CART)!;
  const out = importSongs(target, source, [1])!;
  expect(names(out)).toEqual(["MINE", "THEIRS2"]); // name travels in the DIRECTORY, not the blob
  expect(readSongBlock(out, 1)).toEqual(sparse(8));
  expect(importSongs(target, source, [])).toBe(null);
  expect(importSongs(target, source, [9])).toBe(null); // nothing valid to take
  expect(importSongs(target, new Uint8Array(CART), [0])).toBe(null); // source is not a save
});

test("rename touches the directory only, leaving the heap where it is", () => {
  const sav = buildSav([{ block: sparse(1), name: "OLD" }, { block: sparse(2), name: "KEEP" }], CART)!;
  const out = renameSong(sav, 0, "NEW")!;
  expect(names(out)).toEqual(["NEW", "KEEP"]);
  expect(readSongBlock(out, 0)).toEqual(sparse(1));
  // Byte-identical outside the 8 name bytes - a rename that re-laid the heap would be a needless
  // rewrite of the whole cart, and would churn every blob offset for a cosmetic change.
  const a = sav.slice(), b = out.slice();
  a.fill(0, 32 + 16, 32 + 24);
  b.fill(0, 32 + 16, 32 + 24);
  expect(b).toEqual(a);
});

// --- what a rebuild must not break --------------------------------------------

test("the OPTIONS block at $3F60 survives every song edit", () => {
  // It sits INSIDE bank 0's heap range, so any op that rewrites the heap can eat it - and an early
  // version of the tail-wipe here did exactly that on a 32 KB cart. Losing it resets the user's sync
  // mode, palette and FM setting as a side effect of deleting a song.
  const config = Uint8Array.from([0x43, 0x46, 2, 5, 1, 1, 3, 20, 8, 0x2b]); // 'C''F' + v3 payload
  const sav = buildSav([
    { block: sparse(1), name: "A" },
    { block: sparse(2), name: "B" },
  ], CART, config)!;
  const readConfig = (s: Uint8Array) => s.subarray(CFG_OFF, CFG_OFF + 10).slice();
  expect(readConfig(sav)).toEqual(config);

  for (const [label, out] of [
    ["delete", deleteSong(sav, 0)!],
    ["reorder", reorderSongs(sav, 0, 1)!],
    ["add", addSong(sav, sparse(3), "C")!],
    ["import", importSongs(sav, sav, [0])!],
    ["setCurSlot", setCurSlot(sav, 1)!],
  ] as [string, Uint8Array][]) {
    expect(readConfig(out)).toEqual(config);
    void label;
  }
});

test("a store-raw blob never straddles the bank boundary or grows into the config", () => {
  // Both rules mirror rle_can_save in the cart. Breaking either produces an image that reads back fine
  // HERE and fails on hardware, which is the worst kind of bug this codec can have.
  //
  // Three store-raw songs is the smallest case that exercises the bump: two fit below the config
  // ($0420..$3A20), and the third would run from $3A20 to $5520 - past $3F60 AND across $4000 - so it
  // is pushed to the top of bank 1 instead. The cap check reserves the full 6,912 bytes rather than
  // the packed size, exactly as the cart does, which is why the bump happens with 1,344 bytes still
  // free below the config.
  const sav = buildSav([1, 2, 3].map((i) => ({ block: dense(i), name: "D" + i })), CART)!;
  const off = (slot: number) => 1056 + (sav[32 + slot * 32 + 2] | (sav[32 + slot * 32 + 3] << 8));
  const len = (slot: number) => sav[32 + slot * 32 + 4] | (sav[32 + slot * 32 + 5] << 8);
  for (const slot of [0, 1, 2]) {
    const start = off(slot), end = start + len(slot);
    expect(start < 0x4000 && end > 0x4000).toBe(false); // never straddles
    if (start < 0x4000) expect(end <= CFG_OFF).toBe(true); // a bank-0 blob stays under the config
  }
  expect(off(0)).toBe(0x0420);
  expect(off(1)).toBe(0x1f20);
  expect(off(2)).toBe(0x4000); // bumped to bank 1 rather than crossing the config
});

test("an image that will not fit reports failure instead of dropping the tail", () => {
  // A 32 KB cart holds exactly four store-raw songs: two below the config and two in bank 1
  // ($0420, $1F20, $4000, $5B00). The fifth has nowhere to go, and silently keeping the first four
  // would look like success while losing a song the user asked to add.
  const four = buildSav([1, 2, 3, 4].map((i) => ({ block: dense(i), name: "D" + i })), CART);
  expect(four != null).toBeTruthy();
  expect(names(four!).length).toBe(4);

  expect(buildSav([1, 2, 3, 4, 5].map((i) => ({ block: dense(i), name: "D" + i })), CART)).toBe(null);
  expect(addSong(four!, dense(5), "D5")).toBe(null);
  expect(importSongs(four!, four!, [0])).toBe(null);
  // ...and a refused op leaves the caller's image untouched, since every op returns a NEW buffer.
  expect(names(four!).length).toBe(4);
});

test("blockChecksum is the cart's 16-bit LE sum", () => {
  expect(blockChecksum(Uint8Array.from([1, 2, 3]))).toBe(6);
  expect(blockChecksum(new Uint8Array(SMDJ4_BLOCK_LEN).fill(0xff))).toBe((SMDJ4_BLOCK_LEN * 0xff) & 0xffff);
});
