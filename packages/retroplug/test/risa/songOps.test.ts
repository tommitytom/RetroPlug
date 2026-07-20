// risa catalog write ops (M2 codec core) — byte-identity vs risa's own catalog.js (SaveImage) oracle,
// plus structural + round-trip + error checks. Base fixture is the 5-record legacy catalog; the input
// record for add/replace is the 4XTREME record. Mirrors test/systems/lsdj-songs.test.ts (byte-level ops).
import { test, expect } from "../../testing/harness";
import { savBytes } from "./fixtures";
import { opBytes } from "./opFixtures";
import {
  deleteSongInSav,
  moveSongInSav,
  addSongRecordToSav,
  replaceSongRecordInSav,
  songRecordBytes,
} from "../../src/risaSongOps";
import { listSongs, chooseCatalogLayout, normalizeSaveContainer, kSaveSize } from "../../src/risaSav";

function sameBytes(a: Uint8Array, b: Uint8Array, label: string): void {
  expect(a.length).toBe(b.length);
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) throw new Error(`${label}: bytes differ at 0x${i.toString(16)} mine=${a[i]} gold=${b[i]}`);
  }
}
const names = (sav: Uint8Array) => listSongs(sav).map((s) => s.name);

const MULTI = () => savBytes("multi_legacy"); // HOU8, HOU, DBZ, DBZ2-F, FUNK0 (legacy v1)
const XTREME = () => opBytes("xtreme_record"); // a whole 4XTREME record (3226 B)

test("deleteSongInSav removes+compacts a record, byte-identical to the risa oracle", () => {
  const out = deleteSongInSav(MULTI(), 2);
  sameBytes(out, opBytes("del_multi_2"), "delete");
  expect(names(out)).toEqual(["HOU8", "HOU", "DBZ2-F", "FUNK0"]);
});

test("moveSongInSav reorders records, byte-identical to the oracle", () => {
  const out = moveSongInSav(MULTI(), 0, 3);
  sameBytes(out, opBytes("move_multi_0to3"), "move");
  expect(names(out)).toEqual(["HOU", "DBZ", "DBZ2-F", "HOU8", "FUNK0"]);
});

test("addSongRecordToSav appends a record, byte-identical to the oracle", () => {
  const out = addSongRecordToSav(MULTI(), XTREME());
  sameBytes(out, opBytes("append_multi_4x"), "append");
  expect(names(out)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0", "4XTREME"]);
});

test("replaceSongRecordInSav overwrites a slot, byte-identical to the oracle", () => {
  const out = replaceSongRecordInSav(MULTI(), 1, XTREME());
  sameBytes(out, opBytes("replace_multi_1_4x"), "replace");
  expect(names(out)).toEqual(["HOU8", "4XTREME", "DBZ", "DBZ2-F", "FUNK0"]);
});

test("songRecordBytes extracts a whole record whose length header matches", () => {
  const rec = songRecordBytes(MULTI(), 2)!; // DBZ, length 2262
  expect(rec != null).toBeTruthy();
  expect(rec[0] | (rec[1] << 8)).toBe(rec.length); // u16 length header == byte length
  expect(rec.length).toBe(2262);
  // Re-adding an extracted record round-trips its name into the catalog.
  const readded = addSongRecordToSav(deleteSongInSav(MULTI(), 2), rec);
  expect(names(readded)).toEqual(["HOU8", "HOU", "DBZ2-F", "FUNK0", "DBZ"]);
  expect(listSongs(readded).length).toBe(5);
});

test("the ops do not mutate the caller's input buffer", () => {
  const input = MULTI();
  const before = names(input);
  deleteSongInSav(input, 0);
  expect(names(input)).toEqual(before); // input untouched — ops clone via normalizeSaveContainer
});

test("addSongRecordToSav initializes a current (v2) catalog on a battery that has none", () => {
  const blank = new Uint8Array(kSaveSize); // no RSAV magic anywhere
  const out = addSongRecordToSav(blank, XTREME());
  expect(names(out)).toEqual(["4XTREME"]);
  expect(chooseCatalogLayout(normalizeSaveContainer(out).save)?.key).toBe("current");
});

test("write ops reject out-of-range slots and no-op an equal move", () => {
  expect(() => deleteSongInSav(MULTI(), 9)).toThrow();
  expect(() => moveSongInSav(MULTI(), 0, 9)).toThrow();
  expect(() => replaceSongRecordInSav(MULTI(), 99, XTREME())).toThrow();
  // move to the same slot is a no-op: result equals the normalized (unedited) input.
  sameBytes(moveSongInSav(MULTI(), 2, 2), normalizeSaveContainer(MULTI()).save, "move-noop");
});

test("replaceSongRecordInSav rejects a record whose length header lies", () => {
  const bad = XTREME().slice();
  bad[0] = (bad[0] + 1) & 0xff; // corrupt the u16 length header so it != byte length
  expect(() => replaceSongRecordInSav(MULTI(), 1, bad)).toThrow();
});
