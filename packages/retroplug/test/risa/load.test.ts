// risa load-to-working op — loadSongToWorkingInSav expands a saved song's payload into the working-song
// region (WRAM banks 0-3) of a fresh battery, preserving the catalog (banks 4-7). Requires the current
// (v2 @0x8000) layout; a legacy battery (catalog @0x6000 overlaps banks 0-3) or a bad slot returns null.
import { test, expect } from "../../testing/harness";
import { savBytes } from "./fixtures";
import { loadSongToWorkingInSav } from "../../src/risaSongOps";
import { expandRecordToWorking, recordBytesAt, normalizeSaveContainer, CURRENT_LAYOUT } from "../../src/risaSav";

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

test("loadSongToWorkingInSav splices the expanded song into banks 0-3 and keeps the catalog (v2)", () => {
  const input = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  const out = loadSongToWorkingInSav(input, 0);
  expect(out).toBeTruthy();
  expect(out!.length).toBe(0x10000);

  // Banks 0-3 == the expanded record, EXCEPT the 'current entry' link byte (see below); catalog banks 4-7 ==
  // the input's, untouched.
  const expectedWorking = expandRecordToWorking(recordBytesAt(input, CURRENT_LAYOUT, 0)!);
  const working = out!.slice(0, 0x8000);
  expect(working.every((b, i) => i === 0x3e94 || b === expectedWorking[i])).toBe(true);
  expect(sameBytes(out!.slice(0x8000), input.slice(0x8000))).toBe(true);

  // The working-song 'N8T' magic is stamped so a cold boot accepts it (bank 1 0x1E80).
  expect(Array.from(out!.slice(0x3e80, 0x3e84))).toEqual([0x4e, 0x38, 0x54, 0x0c]);
  // And the working song is LINKED to the slot it came from (bank 1 0x1E94), as the cart's own Load does - a
  // fresh record expansion carries 0xFF there, so this is the one byte that differs from it.
  expect(expectedWorking[0x3e94]).toBe(0xff);
  expect(out![0x3e94]).toBe(0);
});

test("loadSongToWorkingInSav does not mutate the caller's input buffer", () => {
  const input = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  const copy = input.slice();
  loadSongToWorkingInSav(input, 0);
  expect(sameBytes(input, copy)).toBe(true);
});

test("loadSongToWorkingInSav returns null for an out-of-range slot", () => {
  const input = savBytes("v2_blumarbl"); // one song (BLUMARBL) — index 1 is empty
  expect(loadSongToWorkingInSav(input, 1)).toBe(null);
  expect(loadSongToWorkingInSav(input, -1)).toBe(null);
});

test("loadSongToWorkingInSav returns null for a legacy-layout battery (working song would overlap it)", () => {
  // The live readSram the menu operates on is always current-layout (the firmware migrates on boot); a raw
  // legacy catalog (@0x6000, banks 3-7) can't host a banks-0-3 working song, so Load safely no-ops.
  expect(loadSongToWorkingInSav(savBytes("multi_legacy"), 0)).toBe(null);
});

test("loadSongToWorkingInSav returns null for an unrecognized / catalog-less battery", () => {
  expect(loadSongToWorkingInSav(new Uint8Array(0x10000), 0)).toBe(null); // blank, no catalog
  expect(loadSongToWorkingInSav(new Uint8Array(10), 0)).toBe(null); // unknown container size
});
