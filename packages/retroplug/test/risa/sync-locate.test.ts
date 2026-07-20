// The pure risa host-sync PPQ→locate math (risaSync.ts): phrase = 4 quarters, 16 chain rows per song row.
import { test, expect } from "../../testing/harness";
import { risaLocate, risaArmPacket, RISA_LOCATE_STATUS, RISA_LOCATE_SUB } from "../../src/risaSync";

test("risaLocate maps absolute PPQ to (songRow, chainRow)", () => {
  expect(risaLocate(0)).toEqual({ songRow: 0, chainRow: 0 });
  expect(risaLocate(3.99)).toEqual({ songRow: 0, chainRow: 0 }); // still phrase 0 (floor)
  expect(risaLocate(4)).toEqual({ songRow: 0, chainRow: 1 }); // phrase 1
  expect(risaLocate(60)).toEqual({ songRow: 0, chainRow: 15 }); // phrase 15 — last chain of song row 0
  expect(risaLocate(64)).toEqual({ songRow: 1, chainRow: 0 }); // phrase 16 → next song row
  expect(risaLocate(2047)).toEqual({ songRow: 31, chainRow: 15 }); // phrase 511
  expect(risaLocate(2048)).toEqual({ songRow: 32, chainRow: 0 }); // phrase 512
});

test("risaLocate clamps negative / pre-roll ppq to phrase 0", () => {
  expect(risaLocate(-10)).toEqual({ songRow: 0, chainRow: 0 });
});

test("risaLocate masks songRow to 7 bits (wraps past 0x7f)", () => {
  // phrase 2048 → songRow 128 → masked to 0. (Past risa's real song length, but the mask must hold.)
  expect(risaLocate(2048 * 4)).toEqual({ songRow: 0, chainRow: 0 });
});

test("risaArmPacket is F9 52 songRow chainRow, masked", () => {
  expect(risaArmPacket({ songRow: 3, chainRow: 7 })).toEqual([RISA_LOCATE_STATUS, RISA_LOCATE_SUB, 3, 7]);
  expect(risaArmPacket({ songRow: 0x7f, chainRow: 0x0f })).toEqual([0xf9, 0x52, 0x7f, 0x0f]);
});
