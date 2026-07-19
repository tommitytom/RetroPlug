// risa save-catalog reader (M1) — golden tests. Fixtures are real risa batteries; goldens are risa's
// own SaveImage/analyze_save.py output (two independent oracles agree — see fixtures.ts). Mirrors
// test/lsdj/lsdsng.test.ts (listSongs is the risa analog of LSDj listProjects).
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import { savBytes } from "./fixtures";
import {
  listSongs,
  parseCatalog,
  normalizeSaveContainer,
  chooseCatalogLayout,
  decodeSongName,
  CURRENT_LAYOUT,
  kSaveSize,
} from "../../src/risaSav";
import goldV2 from "./golden/v2_blumarbl.json";
import goldLegacy from "./golden/legacy_4xtreme.json";
import goldMulti from "./golden/multi_legacy.json";

test("listSongs reads the current (v2 @0x8000) catalog and matches the oracle", () => {
  const songs = listSongs(savBytes("v2_blumarbl"));
  deepEqual(songs, goldV2, "v2_blumarbl");
  expect(songs.length).toBe(1);
  expect(songs[0].name).toBe("BLUMARBL");
  expect(songs[0].version).toBe(5);
  expect(songs[0].length).toBe(6905);
});

test("listSongs falls back to the legacy (v1 @0x6000) catalog used by the demo .srm files", () => {
  const songs = listSongs(savBytes("legacy_4xtreme"));
  deepEqual(songs, goldLegacy, "legacy_4xtreme");
  expect(songs[0].name).toBe("4XTREME");
});

test("listSongs walks a multi-record legacy catalog in order", () => {
  const songs = listSongs(savBytes("multi_legacy"));
  deepEqual(songs, goldMulti, "multi_legacy");
  expect(songs.map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ", "DBZ2-F", "FUNK0"]);
  // indices are the record ordinals, in walk order
  expect(songs.map((s) => s.index)).toEqual([0, 1, 2, 3, 4]);
});

test("chooseCatalogLayout picks current over legacy and the right region per fixture", () => {
  const v2 = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  const legacy = normalizeSaveContainer(savBytes("legacy_4xtreme")).save;
  expect(chooseCatalogLayout(v2)?.key).toBe("current");
  expect(chooseCatalogLayout(legacy)?.key).toBe("legacy");
});

test("listSongs returns [] for a blank battery, a 32 KB rescue dump, and an unknown size", () => {
  expect(listSongs(new Uint8Array(kSaveSize)).length).toBe(0); // blank 64 KB: no RSAV magic
  expect(listSongs(new Uint8Array(0x8000)).length).toBe(0); // 32 KB rescue -> zero-extended, no catalog
  expect(listSongs(new Uint8Array(123)).length).toBe(0); // unrecognized container size -> tolerant []
});

test("normalizeSaveContainer maps every known container to a 64 KB image", () => {
  expect(normalizeSaveContainer(new Uint8Array(0x8000)).save.length).toBe(kSaveSize); // 32 KB rescue
  expect(normalizeSaveContainer(new Uint8Array(kSaveSize)).save.length).toBe(kSaveSize); // 64 KB
  expect(normalizeSaveContainer(savBytes("v2_blumarbl")).save.length).toBe(kSaveSize); // 65 KB + tail
  expect(normalizeSaveContainer(new Uint8Array(0x40000)).save.length).toBe(kSaveSize); // 256 KB Pocket
  expect(() => normalizeSaveContainer(new Uint8Array(999))).toThrow();
});

test("parseCatalog is strict: rejects a catalog whose used-count overflows the region", () => {
  const save = new Uint8Array(kSaveSize);
  save.set([0x52, 0x53, 0x41, 0x56], 0x8000); // "RSAV"
  save[0x8004] = 2; // version
  save[0x8005] = 1; // count
  save[0x8006] = 0xff; // used = 0xFFFF -> exceeds region (max 0x7F00)
  save[0x8007] = 0xff;
  expect(() => parseCatalog(save, CURRENT_LAYOUT)).toThrow();
  // listSongs swallows the parse failure and reports no songs rather than throwing.
  expect(listSongs(save).length).toBe(0);
});

test("parseCatalog rejects a record walk that doesn't fill exactly used bytes", () => {
  const save = new Uint8Array(kSaveSize);
  save.set([0x52, 0x53, 0x41, 0x56], 0x8000);
  save[0x8004] = 2; // version
  save[0x8005] = 1; // count = 1
  save[0x8006] = 0x20; // used = 0x20 (32) but the single record below is only 0x10
  save[0x8100] = 0x10; // record length = 0x10 -> walk ends at 0x110, not 0x120
  expect(() => parseCatalog(save, CURRENT_LAYOUT)).toThrow();
});

test("decodeSongName trims space-padding and falls back to UNTITLED", () => {
  expect(decodeSongName(new Uint8Array([0x41, 0x42, 0x20, 0x20, 0, 0, 0, 0]))).toBe("AB");
  expect(decodeSongName(new Uint8Array(8))).toBe("UNTITLED"); // all NUL
  expect(decodeSongName(new Uint8Array([0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20]))).toBe("UNTITLED");
});
