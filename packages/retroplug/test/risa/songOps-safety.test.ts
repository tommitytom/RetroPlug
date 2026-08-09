// risa save DATA-SAFETY suite: prove catalog edits never clobber a save, lose a song, or damage the live
// working song. The existing songOps tests only cover the LEGACY layout (multi_legacy) with full-image
// goldens; the PRIMARY live layout is CURRENT v2 (@0x8000, what readSram returns after firmware migration),
// which had zero multi-song op coverage. Here we build a v2 multi-song catalog, assert every UNTOUCHED record
// stays byte-identical + the catalog re-parses (used/count in sync), prove the working song (banks 0-3) stays
// byte-identical across catalog edits (incl. addSongRecordToSav's blank-catalog init), run a SEQUENTIAL chain,
// and cover normalizeSaveContainer preservation + malformed-input + empty/single edges.
import { test, expect } from "../../testing/harness";
import { savBytes } from "./fixtures";
import {
  normalizeSaveContainer,
  listSongs,
  chooseCatalogLayout,
  parseCatalog,
  recordBytesAt,
  expandRecordToWorking,
  makeEmptySave,
  CURRENT_LAYOUT,
} from "../../src/risaSav";
import { songRecordBytes, deleteSongInSav, moveSongInSav, replaceSongRecordInSav, addSongRecordToSav, loadSongToWorkingInSav, importSongsFromSav, workingSongSlot, saveWorkingToCatalog, workingSongRecord } from "../../src/risaSongOps";
import { sameBytes } from "../_bytes";

const battery = (key: "v2_blumarbl" | "multi_legacy" | "legacy_4xtreme") => normalizeSaveContainer(savBytes(key)).save;
const blumarblRecord = () => songRecordBytes(battery("v2_blumarbl"), 0)!;
const recAt = (sav: Uint8Array, i: number) => recordBytesAt(sav, chooseCatalogLayout(sav)!, i)!;

// A CURRENT-v2 catalog holding 5 distinct songs (HOU8/HOU/DBZ/DBZ2-F/FUNK0), built by re-adding the legacy
// fixture's records into a blank v2 catalog — the multi-song v2 battery the fixtures don't ship.
function v2Multi(): Uint8Array {
  const legacy = battery("multi_legacy");
  const n = listSongs(legacy).length;
  let v2 = makeEmptySave();
  for (let i = 0; i < n; i++) v2 = addSongRecordToSav(v2, songRecordBytes(legacy, i)!);
  return v2;
}

// --- v2 multi-song: untouched records stay byte-identical + the catalog re-parses ----------------------

test("v2 delete removes only the target record + keeps every other byte-identical + re-parses", () => {
  const sav = v2Multi();
  const before = [0, 1, 2, 3, 4].map((i) => recAt(sav, i));
  const after = deleteSongInSav(sav, 1); // delete HOU
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU8", "DBZ", "DBZ2-F", "FUNK0"]);
  expect(sameBytes(recAt(after, 0), before[0])).toBe(true); // compacted: survivors byte-identical
  expect(sameBytes(recAt(after, 1), before[2])).toBe(true);
  expect(sameBytes(recAt(after, 2), before[3])).toBe(true);
  expect(sameBytes(recAt(after, 3), before[4])).toBe(true);
  parseCatalog(after, CURRENT_LAYOUT); // throws if used/count desync
});

test("v2 move reorders without altering any record's bytes + re-parses", () => {
  const sav = v2Multi();
  const before = [0, 1, 2, 3, 4].map((i) => recAt(sav, i));
  const after = moveSongInSav(sav, 0, 1); // swap positions 0<->1
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU", "HOU8", "DBZ", "DBZ2-F", "FUNK0"]);
  const now = [0, 1, 2, 3, 4].map((i) => recAt(after, i));
  for (const b of before) expect(now.some((a) => sameBytes(a, b))).toBe(true); // every record still present, byte-exact
  parseCatalog(after, CURRENT_LAYOUT);
});

test("v2 replace swaps one record + leaves the others byte-identical + re-parses", () => {
  const sav = v2Multi();
  const before = [0, 1, 2, 3, 4].map((i) => recAt(sav, i));
  const xtreme = songRecordBytes(battery("legacy_4xtreme"), 0)!;
  const after = replaceSongRecordInSav(sav, 2, xtreme); // replace DBZ
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU8", "HOU", "4XTREME", "DBZ2-F", "FUNK0"]);
  expect(sameBytes(recAt(after, 0), before[0])).toBe(true);
  expect(sameBytes(recAt(after, 1), before[1])).toBe(true);
  expect(sameBytes(recAt(after, 2), xtreme)).toBe(true); // the new record
  expect(sameBytes(recAt(after, 3), before[3])).toBe(true);
  expect(sameBytes(recAt(after, 4), before[4])).toBe(true);
  parseCatalog(after, CURRENT_LAYOUT);
});

test("v2 add appends without altering any existing record + re-parses", () => {
  const sav = v2Multi();
  const before = [0, 1, 2, 3, 4].map((i) => recAt(sav, i));
  const rec = blumarblRecord();
  const after = addSongRecordToSav(sav, rec);
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0", "BLUMARBL"]);
  for (let i = 0; i < 5; i++) expect(sameBytes(recAt(after, i), before[i])).toBe(true); // existing untouched
  expect(sameBytes(recAt(after, 5), rec)).toBe(true); // appended byte-exact
  parseCatalog(after, CURRENT_LAYOUT);
});

// --- working song (banks 0-3) isolation from catalog edits ---------------------------------------------

test("every catalog edit leaves the live working song (banks 0-3) byte-identical", () => {
  const seeded = v2Multi();
  seeded.set(expandRecordToWorking(blumarblRecord()), 0); // a live working song in banks 0-3
  const working = seeded.slice(0, 0x8000);
  const rec = blumarblRecord();
  const ops: Array<(s: Uint8Array) => Uint8Array> = [
    (s) => deleteSongInSav(s, 1),
    (s) => moveSongInSav(s, 0, 1),
    (s) => replaceSongRecordInSav(s, 0, rec),
    (s) => addSongRecordToSav(s, rec),
  ];
  for (const op of ops) {
    const after = op(seeded);
    expect(sameBytes(after.slice(0, 0x8000), working)).toBe(true); // banks 0-3 untouched by the catalog edit
    parseCatalog(after, CURRENT_LAYOUT);
  }
});

test("addSongRecordToSav's blank-catalog init preserves a live working song (banks 0-3)", () => {
  // A battery carrying a working song but NO RSAV catalog — addSongRecordToSav must initialize the catalog
  // (banks 4-7) WITHOUT clobbering the live song (banks 0-3).
  const rec = blumarblRecord();
  const noCatalog = new Uint8Array(0x10000);
  noCatalog.set(expandRecordToWorking(rec), 0);
  const working = noCatalog.slice(0, 0x8000);
  expect(chooseCatalogLayout(noCatalog)).toBe(null); // no catalog yet
  const after = addSongRecordToSav(noCatalog, rec);
  expect(chooseCatalogLayout(after)).toEqual(CURRENT_LAYOUT); // initialized as v2
  expect(listSongs(after).map((s) => s.name)).toEqual(["BLUMARBL"]);
  expect(sameBytes(after.slice(0, 0x8000), working)).toBe(true); // the live working song survived
  parseCatalog(after, CURRENT_LAYOUT);
});

// --- importSongsFromSav (the Songs "Add from .sav" subset importer): append-only, never clobber ---------

test("import appends only the SELECTED source records + keeps every existing record byte-identical", () => {
  const target = v2Multi(); // 5 songs
  const before = [0, 1, 2, 3, 4].map((i) => recAt(target, i));
  const src = battery("multi_legacy"); // a LEGACY (v1) source → the cross-version import path
  const after = importSongsFromSav(target, src, [0, 2]); // HOU8 + DBZ
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0", "HOU8", "DBZ"]);
  for (let i = 0; i < 5; i++) expect(sameBytes(recAt(after, i), before[i])).toBe(true); // existing 5 untouched
  expect(sameBytes(recAt(after, 5), songRecordBytes(src, 0)!)).toBe(true); // appended == the source record
  expect(sameBytes(recAt(after, 6), songRecordBytes(src, 2)!)).toBe(true);
  expect(chooseCatalogLayout(after)).toEqual(CURRENT_LAYOUT); // target stays v2 despite the v1 source
  parseCatalog(after, CURRENT_LAYOUT); // used/count in sync
});

test("import leaves the live working song (banks 0-3) byte-identical", () => {
  const target = v2Multi();
  target.set(expandRecordToWorking(blumarblRecord()), 0); // a live working song
  const working = target.slice(0, 0x8000);
  const after = importSongsFromSav(target, battery("multi_legacy"), [0, 1, 2]);
  expect(sameBytes(after.slice(0, 0x8000), working)).toBe(true); // banks 0-3 untouched by the import
  parseCatalog(after, CURRENT_LAYOUT);
});

test("import mutates neither the source nor the target buffer", () => {
  const target = v2Multi();
  const tBefore = target.slice();
  const src = battery("multi_legacy");
  const sBefore = src.slice();
  importSongsFromSav(target, src, [0, 1]);
  expect(sameBytes(target, tBefore)).toBe(true); // the target we passed is untouched (a fresh image is returned)
  expect(sameBytes(src, sBefore)).toBe(true); // the source is read-only
});

test("import skips out-of-range source indices without error", () => {
  const after = importSongsFromSav(v2Multi(), battery("multi_legacy"), [0, 99, 2]); // 99 out of range
  expect(listSongs(after).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0", "HOU8", "DBZ"]);
  parseCatalog(after, CURRENT_LAYOUT);
});

test("import into a FULL catalog is a safe best-effort no-op: no throw, existing records byte-identical", () => {
  // Fill a v2 catalog until the next append won't fit (the exact overflow the reviewer flagged: it used to
  // THROW mid-batch and discard everything, wiping songs that already fit).
  let full = v2Multi();
  const rec = blumarblRecord();
  try {
    for (let i = 0; i < 512; i++) full = addSongRecordToSav(full, rec); // stops when writeRecord throws
  } catch {
    /* full — `full` holds the last image that fit */
  }
  const before = listSongs(full);
  const beforeBytes = before.map((_, i) => recAt(full, i));
  // A multi-song import into the full catalog: best-effort fill must not throw and must not touch a byte
  // of any existing record.
  const after = importSongsFromSav(full, battery("multi_legacy"), [0, 1, 2, 3, 4]);
  expect(listSongs(after).length >= before.length).toBe(true); // never LOSES songs
  for (let i = 0; i < before.length; i++) expect(sameBytes(recAt(after, i), beforeBytes[i])).toBe(true); // existing intact
  parseCatalog(after, CURRENT_LAYOUT); // still a valid catalog
});

// --- a long SEQUENTIAL chain: no cumulative drift, working song never clobbered -------------------------

test("a long chain of v2 catalog edits keeps the catalog parseable + the working song byte-identical", () => {
  const rec = blumarblRecord();
  let sav = v2Multi();
  sav.set(expandRecordToWorking(rec), 0); // seed a working song
  const working = sav.slice(0, 0x8000);
  const check = () => {
    parseCatalog(sav, CURRENT_LAYOUT); // used/count stay in sync
    expect(sameBytes(sav.slice(0, 0x8000), working)).toBe(true); // working song never clobbered by a catalog op
  };

  sav = addSongRecordToSav(sav, rec); check(); // 6 songs
  sav = moveSongInSav(sav, 0, 1); check();
  sav = replaceSongRecordInSav(sav, 2, rec); check();
  sav = deleteSongInSav(sav, 4); check(); // 5 songs
  sav = deleteSongInSav(sav, 0); check(); // 4 songs
  expect(listSongs(sav).length).toBe(4);

  const loaded = loadSongToWorkingInSav(sav, 0)!; // this DOES rewrite banks 0-3 (intended) — catalog preserved
  expect(loaded != null).toBe(true);
  parseCatalog(loaded, CURRENT_LAYOUT);
  expect(listSongs(loaded).length).toBe(4); // banks 4-7 (the catalog) untouched by the load
});

// --- normalizeSaveContainer: an independent copy that preserves the payload ----------------------------

test("normalizeSaveContainer returns an independent 64 KB copy that preserves the payload", () => {
  const b = battery("v2_blumarbl");
  const out = normalizeSaveContainer(b);
  expect(out.save.length).toBe(0x10000);
  expect(sameBytes(out.save, b)).toBe(true); // payload preserved
  const original = b[0];
  out.save[0] ^= 0xff; // mutate the result...
  expect(b[0]).toBe(original); // ...the input buffer is distinct (unchanged)

  const half = b.slice(0, 0x8000); // a 32 KB rescue dump
  const ext = normalizeSaveContainer(half).save;
  expect(ext.length).toBe(0x10000); // zero-extended
  expect(sameBytes(ext.slice(0, 0x8000), half)).toBe(true); // real 32 KB preserved

  expect(() => normalizeSaveContainer(new Uint8Array(999))).toThrow(); // unknown size never silently truncates
});

// --- malformed input never corrupts the battery --------------------------------------------------------

test("malformed input is safe: reads return null; bad records throw before touching the battery", () => {
  const sav = v2Multi();
  expect(songRecordBytes(sav, 99)).toBe(null); // out-of-range index
  expect(songRecordBytes(new Uint8Array(999), 0)).toBe(null); // unrecognized container
  expect(loadSongToWorkingInSav(sav, 99)).toBe(null); // out-of-range
  expect(loadSongToWorkingInSav(battery("legacy_4xtreme"), 0)).toBe(null); // legacy layout can't host a working song

  const before = sav.slice();
  expect(() => addSongRecordToSav(sav, new Uint8Array([1, 2, 3]))).toThrow(); // record shorter than its header
  expect(() => replaceSongRecordInSav(sav, 0, new Uint8Array([1, 2, 3]))).toThrow();
  expect(sameBytes(sav, before)).toBe(true); // the source battery is never mutated (ops clone first)
});

// --- empty / single-song edges -------------------------------------------------------------------------

test("empty and single-song catalogs handle delete/move/load safely", () => {
  const empty = makeEmptySave(); // 0 songs, v2
  expect(listSongs(empty).length).toBe(0);
  expect(() => deleteSongInSav(empty, 0)).toThrow(); // out-of-range on an empty catalog
  expect(() => moveSongInSav(empty, 0, 1)).toThrow();
  expect(loadSongToWorkingInSav(empty, 0)).toBe(null); // nothing to load

  const one = addSongRecordToSav(makeEmptySave(), blumarblRecord());
  expect(listSongs(one).map((s) => s.name)).toEqual(["BLUMARBL"]);
  const gone = deleteSongInSav(one, 0); // delete the last remaining record
  expect(listSongs(gone).length).toBe(0);
  parseCatalog(gone, CURRENT_LAYOUT); // count/used zeroed consistently
});

// --- the working song's catalog link survives a positional edit ---------------------------------------
// risa's catalog is positional: delete compacts and move re-packs, so indices above the edit shift. The
// working song's 'current entry' byte is one of those indices. If it isn't re-pointed it comes to name a
// DIFFERENT song, and saveWorkingToCatalog then overwrites that unrelated song in place - silent data loss
// reachable by ordinary menu use (save -> add -> delete an earlier song -> save again).

const CUR = 0x2000 + 0x1e94;

function v2WithSongs(): Uint8Array {
  // Three distinct records so a shift is observable by NAME.
  let sav = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  const rec = songRecordBytes(sav, 0)!;
  sav = addSongRecordToSav(sav, rec);
  sav = addSongRecordToSav(sav, rec);
  return sav;
}

test("deleting an EARLIER song re-points the working song's link (it must not slide onto a neighbour)", () => {
  const sav = v2WithSongs();
  sav[CUR] = 2; // the working song is the one saved in slot 2
  const target = songRecordBytes(sav, 2)!;

  const after = deleteSongInSav(sav, 0); // everything above 0 shifts down one

  expect(workingSongSlot(after)).toBe(1); // 2 -> 1, still the SAME song
  expect([...songRecordBytes(after, workingSongSlot(after))!]).toEqual([...target]);
});

test("deleting the linked song itself unlinks the working song rather than aiming it elsewhere", () => {
  const sav = v2WithSongs();
  sav[CUR] = 1;
  const after = deleteSongInSav(sav, 1);
  expect(workingSongSlot(after)).toBe(-1); // unlinked, exactly like LSDj clears activeProjectIndex
});

test("deleting a LATER song leaves the link alone", () => {
  const sav = v2WithSongs();
  sav[CUR] = 0;
  expect(workingSongSlot(deleteSongInSav(sav, 2))).toBe(0);
});

test("reordering follows the working song's link through the move", () => {
  const sav = v2WithSongs();

  // The moved record itself.
  const a = sav.slice(); a[CUR] = 0;
  expect(workingSongSlot(moveSongInSav(a, 0, 2))).toBe(2);

  // Moved from below to above it: it shifts up one.
  const b = sav.slice(); b[CUR] = 2;
  expect(workingSongSlot(moveSongInSav(b, 0, 2))).toBe(1);

  // Moved from above to below it: it shifts down one.
  const c = sav.slice(); c[CUR] = 0;
  expect(workingSongSlot(moveSongInSav(c, 2, 0))).toBe(1);

  // Outside the disturbed span: untouched.
  const d = sav.slice(); d[CUR] = 2;
  expect(workingSongSlot(moveSongInSav(d, 0, 1))).toBe(2);
});

test("an UNLINKED working song stays unlinked through delete and move", () => {
  const sav = v2WithSongs();
  sav[CUR] = 0xff;
  expect(workingSongSlot(deleteSongInSav(sav, 0))).toBe(-1);
  expect(workingSongSlot(moveSongInSav(sav, 0, 2))).toBe(-1);
});

test("save-then-delete-then-save does NOT overwrite an unrelated song (the regression this guards)", () => {
  // The stale index must stay IN RANGE for this to bite - an out-of-range one harmlessly falls through to
  // the append path. Three songs, working linked to 1, delete 0: count drops to 2 and a stale 1 is still
  // < 2, so the in-place branch would fire on whatever slid into index 1.
  const sav = v2WithSongs();
  sav[CUR] = 1;
  const linkedSong = songRecordBytes(sav, 1)!;
  const bystander = songRecordBytes(sav, 2)!; // this is what shifts into index 1

  const afterDelete = deleteSongInSav(sav, 0);
  expect([...songRecordBytes(afterDelete, 1)!]).toEqual([...bystander]); // it really did slide into 1
  expect(workingSongSlot(afterDelete)).toBe(0); // and the link followed its own song down to 0
  expect([...songRecordBytes(afterDelete, 0)!]).toEqual([...linkedSong]);

  const afterSave = saveWorkingToCatalog(afterDelete);
  // The bystander is untouched: it was never the working song, and nothing may overwrite it.
  expect([...songRecordBytes(afterSave, 1)!]).toEqual([...bystander]);
  // The working song is linked to a slot that genuinely holds it.
  const slot = workingSongSlot(afterSave);
  expect(slot >= 0).toBe(true);
  expect([...songRecordBytes(afterSave, slot)!]).toEqual([...workingSongRecord(afterSave)!]);
});
