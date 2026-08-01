// LSDj save DATA-SAFETY suite: prove edits never clobber a save or lose a song. The existing lsdj-songs
// tests run ops on tiny savFrom() batteries (~1 compressed block); here we exercise them against a REAL
// multi-block corpus sav (all.sav = 2 real stored projects), assert every UNTOUCHED song stays byte-for-byte
// identical, prove the edited image still DECODES cleanly (no orphaned blocks / alloc-table corruption), run a
// long SEQUENTIAL chain of edits re-validating after every step, and cover the malformed-input / full / empty
// / single-song edges + swapProjectSlots directly.
import { test, expect } from "../../testing/harness";
import { savBytes } from "../lsdj/fixtures";
import {
  listProjects,
  decompressSlot,
  savFrom,
  decodeSav,
  encodeLsdsngRaw,
  injectSong,
  swapProjectSlots,
  loadSongToWorking,
  savSongName,
} from "../../src/lsdjSav";
import { workingSongName } from "../../src/lsdj/codec/sav";
import { deleteSongInSav, addLsdsngToSav, replaceSongInSav, importAllSongsFromSav, importSongsFromSav, moveSongInSav } from "../../src/lsdjSongOps";
import { sameBytes, firstDiff } from "../_bytes";

// all.sav: two REAL multi-block stored projects — [0] "HAPPY BD", [1] "YOURULE" (each 0x8000 decompressed).
const ALL = () => savBytes("all");
const rawSong = (tempo: number) => savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();
const lsdsng = (name: string, tempo: number) => encodeLsdsngRaw(name, 4, rawSong(tempo));

// --- untouched-song byte-identity + re-decode, on real multi-block songs ------------------------------

test("delete on a real multi-block sav removes only the target + keeps the neighbor byte-identical + re-decodes", () => {
  const sav = ALL();
  const yourule = decompressSlot(sav, 1)!;
  const after = deleteSongInSav(sav, 0); // remove HAPPY BD
  expect(decompressSlot(after, 0)).toBe(null);
  expect(firstDiff(decompressSlot(after, 1)!, yourule)).toBe(-1); // YOURULE untouched, byte-exact
  expect(listProjects(after).map((p) => p.name)).toEqual(["YOURULE"]);
  decodeSav(after); // throws if the alloc table / block chain is corrupt
});

test("add on a real multi-block sav keeps both existing songs byte-identical + round-trips the new song", () => {
  const sav = ALL();
  const [hb, yr] = [decompressSlot(sav, 0)!, decompressSlot(sav, 1)!];
  const song = rawSong(66);
  const after = addLsdsngToSav(sav, encodeLsdsngRaw("ADDED", 5, song))!;
  expect(listProjects(after).map((p) => p.name)).toEqual(["HAPPY BD", "YOURULE", "ADDED"]); // first free slot = 2
  expect(sameBytes(decompressSlot(after, 0)!, hb)).toBe(true);
  expect(sameBytes(decompressSlot(after, 1)!, yr)).toBe(true);
  expect(sameBytes(decompressSlot(after, 2)!, song)).toBe(true); // added song byte-exact
  expect(sameBytes(after.subarray(0, 0x8000), song)).toBe(true); // + loaded into working memory
  decodeSav(after);
});

test("replace on a real multi-block sav swaps only the target + keeps the neighbor byte-identical", () => {
  const sav = ALL();
  const yr = decompressSlot(sav, 1)!;
  const song = rawSong(99);
  const after = replaceSongInSav(sav, 0, encodeLsdsngRaw("REPL", 4, song))!;
  expect(sameBytes(decompressSlot(after, 0)!, song)).toBe(true); // new song in slot 0
  expect(sameBytes(decompressSlot(after, 1)!, yr)).toBe(true); // YOURULE untouched
  expect(listProjects(after).map((p) => p.name)).toEqual(["REPL", "YOURULE"]);
  decodeSav(after);
});

test("move on a real multi-block sav swaps the two songs byte-exact + re-decodes", () => {
  const sav = ALL();
  const [hb, yr] = [decompressSlot(sav, 0)!, decompressSlot(sav, 1)!];
  const after = moveSongInSav(sav, 0, 1)!; // swap list positions 0<->1 (slots 0<->1)
  expect(sameBytes(decompressSlot(after, 0)!, yr)).toBe(true);
  expect(sameBytes(decompressSlot(after, 1)!, hb)).toBe(true);
  expect(listProjects(after).map((p) => p.name)).toEqual(["YOURULE", "HAPPY BD"]);
  decodeSav(after);
});

test("importAllSongsFromSav copies both corpus songs into a fresh sav byte-exact", () => {
  const src = ALL();
  const [hb, yr] = [decompressSlot(src, 0)!, decompressSlot(src, 1)!];
  const dst = importAllSongsFromSav(savFrom({}), src);
  expect(listProjects(dst).map((p) => p.name)).toEqual(["HAPPY BD", "YOURULE"]);
  expect(sameBytes(decompressSlot(dst, 0)!, hb)).toBe(true);
  expect(sameBytes(decompressSlot(dst, 1)!, yr)).toBe(true);
  decodeSav(dst);
});

// --- importSongsFromSav (the Songs "Add from .sav" subset importer): never clobber the user's data --------

// A target battery with one SAVED song (slot 0 "MINE") plus a DISTINCT UNSAVED working song — the case a
// clobber would destroy: importing must leave both untouched.
function targetWithWorking(): { sav: Uint8Array; working: Uint8Array; saved0: Uint8Array } {
  let sav = injectSong(savFrom({}), 0, "MINE", 9, rawSong(88))!; // saved slot 0
  const working = rawSong(200); // an unsaved edit live in working memory, unlike any saved slot
  sav = sav.slice();
  sav.set(working, 0); // working region [0,0x8000) = the unsaved song
  return { sav, working, saved0: decompressSlot(sav, 0)! };
}

test("import copies only the SELECTED source songs into free slots + re-decodes", () => {
  const src = ALL(); // [0] HAPPY BD, [1] YOURULE
  const yr = decompressSlot(src, 1)!;
  const { sav } = targetWithWorking();
  const after = importSongsFromSav(sav, src, [1]); // import YOURULE only
  expect(listProjects(after).map((p) => p.name)).toEqual(["MINE", "YOURULE"]); // MINE kept, only YOURULE added
  expect(sameBytes(decompressSlot(after, 1)!, yr)).toBe(true); // imported song byte-exact (first free slot = 1)
  decodeSav(after);
});

test("import leaves the live WORKING song + every existing saved song byte-identical (no clobber)", () => {
  const { sav, working, saved0 } = targetWithWorking();
  const after = importSongsFromSav(sav, ALL(), [0, 1]); // import both corpus songs
  expect(sameBytes(after.subarray(0, 0x8000), working)).toBe(true); // working memory NOT overwritten
  expect(sameBytes(decompressSlot(after, 0)!, saved0)).toBe(true); // the existing saved song untouched
  expect(after[0x8140]).toBe(sav[0x8140]); // the active-project pointer is left alone
  expect(listProjects(after).map((p) => p.name)).toEqual(["MINE", "HAPPY BD", "YOURULE"]);
  decodeSav(after);
});

test("import mutates NEITHER input buffer (source + target both byte-identical after)", () => {
  const src = ALL();
  const srcBefore = src.slice();
  const { sav } = targetWithWorking();
  const tgtBefore = sav.slice();
  importSongsFromSav(sav, src, [0, 1]);
  expect(sameBytes(src, srcBefore)).toBe(true); // the source sav is read-only
  expect(sameBytes(sav, tgtBefore)).toBe(true); // the target we passed is untouched (a fresh image is returned)
});

test("import into an ALMOST-FULL target fills the last slot then STOPS — no overwrite, no corruption", () => {
  let sav = savFrom({});
  for (let i = 0; i < 31; i++) sav = injectSong(sav, i, `S${i}`, 1, rawSong(40 + i))!; // 31 of 32 slots used
  const existing = Array.from({ length: 31 }, (_, i) => decompressSlot(sav, i)!);
  const src = ALL(); // 2 songs, but only ONE free slot (31) remains
  const after = importSongsFromSav(sav, src, [0, 1]);
  expect(listProjects(after).length).toBe(32); // filled to capacity, not beyond
  for (let i = 0; i < 31; i++) expect(sameBytes(decompressSlot(after, i)!, existing[i])).toBe(true); // all 31 intact
  expect(sameBytes(decompressSlot(after, 31)!, decompressSlot(src, 0)!)).toBe(true); // HAPPY BD took the last slot
  decodeSav(after); // the over-capacity drop left a valid image
});

test("import skips empty/out-of-range source slots without error", () => {
  const src = ALL(); // only slots 0,1 occupied
  const after = importSongsFromSav(savFrom({}), src, [5, 0, 99, 1]); // 5 + 99 are empty/out of range
  expect(listProjects(after).map((p) => p.name)).toEqual(["HAPPY BD", "YOURULE"]); // only the real ones imported
  decodeSav(after);
});

// --- a long SEQUENTIAL chain: an untouched anchor survives arbitrary churn --------------------------------

test("a long chain of edits leaves an untouched anchor byte-identical + a valid image at every step", () => {
  let sav = ALL(); // [0] HAPPY BD, [1] YOURULE — slot 1 is the ANCHOR we never edit
  const anchor = decompressSlot(sav, 1)!;
  let steps = 0;
  const check = () => {
    steps++;
    decodeSav(sav); // valid image (no orphaned blocks / alloc corruption)
    const cur = decompressSlot(sav, 1);
    expect(cur != null && sameBytes(cur, anchor)).toBe(true); // anchor byte-identical
    expect(savSongName(sav, 1)).toBe("YOURULE");
  };

  sav = addLsdsngToSav(sav, lsdsng("AAA", 10))!; check(); // -> slot 2
  sav = addLsdsngToSav(sav, lsdsng("BBB", 20))!; check(); // -> slot 3
  sav = deleteSongInSav(sav, 0); check(); // drop HAPPY BD (slot 0)
  sav = addLsdsngToSav(sav, lsdsng("CCC", 30))!; check(); // reuse slot 0
  sav = replaceSongInSav(sav, 2, lsdsng("DDD", 40))!; check(); // replace AAA @ slot 2
  sav = moveSongInSav(sav, 2, 3)!; check(); // swap positions 2<->3 (slots 2,3) — never touches the anchor slot 1
  sav = loadSongToWorking(sav, 1)!; check(); // load the anchor into working memory
  expect(sameBytes(sav.subarray(0, 0x8000), anchor)).toBe(true); // working memory == anchor
  expect(sav[0x8140]).toBe(1); // active = the anchor's slot

  expect(steps >= 7).toBe(true);
  expect(listProjects(sav).map((p) => p.name).sort()).toEqual(["BBB", "CCC", "DDD", "YOURULE"]);
});

// --- swapProjectSlots directly (only reached via move on a 2-song sav elsewhere) ------------------------

test("swapProjectSlots swaps two occupied slots byte-exact + leaves an unrelated active pointer alone", () => {
  const sav = ALL();
  const [hb, yr] = [decompressSlot(sav, 0)!, decompressSlot(sav, 1)!];
  sav[0x8140] = 0xff; // no active song
  const s = swapProjectSlots(sav, 0, 1);
  expect(sameBytes(decompressSlot(s, 0)!, yr)).toBe(true);
  expect(sameBytes(decompressSlot(s, 1)!, hb)).toBe(true);
  expect(savSongName(s, 0)).toBe("YOURULE");
  expect(s[0x8140]).toBe(0xff); // untouched — referenced neither swapped slot
  decodeSav(s);
});

test("swapProjectSlots no-op guards return an unchanged copy (A===B / out-of-range / no jk-header)", () => {
  const sav = ALL();
  expect(sameBytes(swapProjectSlots(sav, 2, 2), sav)).toBe(true); // A===B
  expect(sameBytes(swapProjectSlots(sav, 0, 99), sav)).toBe(true); // slot out of range
  const blank = new Uint8Array(0x20000);
  expect(sameBytes(swapProjectSlots(blank, 0, 1), blank)).toBe(true); // no 'jk' archive magic
});

// --- malformed input never corrupts the battery --------------------------------------------------------

test("malformed input is a safe no-op: bad files return null and never mutate the source battery", () => {
  const sav = ALL();
  const before = sav.slice();
  const garbage = new Uint8Array([1, 2, 3, 4, 5]);
  expect(addLsdsngToSav(sav, garbage)).toBe(null); // undecodable .lsdsng
  expect(replaceSongInSav(sav, 0, garbage)).toBe(null); // undecodable file
  expect(replaceSongInSav(sav, 99, lsdsng("X", 1))).toBe(null); // slot out of range
  expect(loadSongToWorking(sav, 5)).toBe(null); // empty slot
  expect(decompressSlot(sav, 5)).toBe(null); // empty-slot read
  expect(sameBytes(sav, before)).toBe(true); // the battery is byte-identical — no partial write
});

// --- full / empty / single-song edges ------------------------------------------------------------------

test("a full sav rejects Add without corrupting existing songs", () => {
  let sav = savFrom({});
  const s = rawSong(50);
  for (let i = 0; i < 32; i++) sav = injectSong(sav, i, `S${i}`, 1, s)!; // fill all 32 slots
  expect(listProjects(sav).length).toBe(32);
  const before = sav.slice();
  expect(addLsdsngToSav(sav, lsdsng("NOPE", 1))).toBe(null); // no free slot
  expect(sameBytes(sav, before)).toBe(true);
  decodeSav(sav);
});

test("empty and single-song savs handle delete/move/load safely (no throw, no corruption)", () => {
  const empty = savFrom({});
  expect(listProjects(empty).length).toBe(0);
  expect(moveSongInSav(empty, 0, 1)).toBe(null); // nothing to move
  expect(loadSongToWorking(empty, 0)).toBe(null);
  const d = deleteSongInSav(empty, 0); // a safe no-op
  decodeSav(d);
  expect(listProjects(d).length).toBe(0);

  const one = injectSong(savFrom({}), 0, "ONLY", 1, rawSong(70))!;
  expect(moveSongInSav(one, 0, 1)).toBe(null); // one song → position 1 out of range
  expect(moveSongInSav(one, 0, 0)).toBe(null); // from === to
  const gone = deleteSongInSav(one, 0);
  expect(listProjects(gone).length).toBe(0);
  decodeSav(gone);
});

// --- replace-the-active-slot: the stored song changes, the LIVE working song is NOT lost ---------------

test("replacing the active slot updates the stored song but leaves working memory intact (no data loss)", () => {
  const withWorking = loadSongToWorking(ALL(), 0)!; // load HAPPY BD; active = 0
  const working = withWorking.subarray(0, 0x8000).slice();
  const song = rawSong(77);
  const after = replaceSongInSav(withWorking, 0, encodeLsdsngRaw("NEW", 3, song))!;
  expect(sameBytes(decompressSlot(after, 0)!, song)).toBe(true); // stored slot 0 = the new song
  expect(sameBytes(after.subarray(0, 0x8000), working)).toBe(true); // live working song NOT clobbered
  expect(after[0x8140]).toBe(0); // active pointer unchanged
  decodeSav(after);
});

// --- header readers ------------------------------------------------------------------------------------

test("workingSongName reflects the active slot; null when none / out-of-range", () => {
  const loaded = loadSongToWorking(ALL(), 1)!;
  expect(workingSongName(loaded)).toBe("YOURULE");
  const none = ALL(); none[0x8140] = 0xff;
  expect(workingSongName(none)).toBe(null);
  const oor = ALL(); oor[0x8140] = 40; // >= 32
  expect(workingSongName(oor)).toBe(null);
});
