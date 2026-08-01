// The pure risa host-sync PPQ→locate math (risaSync.ts), against the mapping risa's protocol doc
// specifies: absoluteClock = floor(ppq*24), phrase = 96 clocks (4 quarters), 16 chain rows per song row,
// and tickOffset = the exact position on the fixed six-clocks-per-row grid within the phrase.
import { test, expect } from "../../testing/harness";
import { risaLocate, risaArmPacket, RISA_LOCATE_STATUS, RISA_LOCATE_SUB } from "../../src/risaSync";

const at = (songRow: number, chainRow: number, tickOffset: number, absoluteClock: number) =>
  ({ songRow, chainRow, tickOffset, absoluteClock });

test("risaLocate maps absolute PPQ to (songRow, chainRow, tickOffset)", () => {
  expect(risaLocate(0)).toEqual(at(0, 0, 0, 0));
  expect(risaLocate(3.99)).toEqual(at(0, 0, 95, 95)); // still phrase 0, but its LAST grid position
  expect(risaLocate(4)).toEqual(at(0, 1, 0, 96)); // phrase 1
  expect(risaLocate(60)).toEqual(at(0, 15, 0, 1440)); // phrase 15 — last chain of song row 0
  expect(risaLocate(64)).toEqual(at(1, 0, 0, 1536)); // phrase 16 → next song row
  expect(risaLocate(2047)).toEqual(at(31, 15, 72, 49128)); // phrase 511, three quarters into it
  expect(risaLocate(2044)).toEqual(at(31, 15, 0, 49056)); // the phrase-511 boundary itself
  expect(risaLocate(2048)).toEqual(at(32, 0, 0, 49152)); // phrase 512
});

test("risaLocate resolves the six-clock grid inside a phrase", () => {
  expect(risaLocate(0.25).tickOffset).toBe(6); // one row in
  expect(risaLocate(0.5).tickOffset).toBe(12);
  expect(risaLocate(1).tickOffset).toBe(24); // one quarter = four rows
  expect(risaLocate(95 / 24)).toEqual(at(0, 0, 0x5f, 95)); // the documented maximum tick offset
});

test("risaLocate clamps negative / pre-roll ppq to clock 0", () => {
  expect(risaLocate(-10)).toEqual(at(0, 0, 0, 0));
});

test("risaLocate masks songRow to 7 bits (wraps past 0x7f)", () => {
  // phrase 2048 → songRow 128 → masked to 0. (Past risa's real song length, but the mask must hold.)
  expect(risaLocate(2048 * 4)).toEqual(at(0, 0, 0, 196608));
});

test("risaArmPacket is the 5-byte F9 52 songRow chainRow tickOffset, masked", () => {
  expect(risaArmPacket(at(3, 7, 42, 0))).toEqual([RISA_LOCATE_STATUS, RISA_LOCATE_SUB, 3, 7, 42]);
  expect(risaArmPacket(at(0x7f, 0x0f, 0x5f, 0))).toEqual([0xf9, 0x52, 0x7f, 0x0f, 0x5f]);
});
