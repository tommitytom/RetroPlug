// genmddj's IN24 level word: counter bit 0 on TR ($A10005 bit 5), bit 1 on TH (bit 6), every other pin released
// (src/main.asm:9438 sync_read: `move.b $A10005,d0; lsr.b #5,d0; andi.b #3,d0`). Active high at the port: a set bit in
// the counter is a HIGH line, which is the same polarity as smsggdj's (active high, idle = counter 3); only the pin bits
// differ, because the two ports are different ports.
import { test, expect } from "@retroplug/testing";
import { mdSyncLevels, GENMDDJ_SYNC_COUNTER_MOD, GENMDDJ_SYNC_IDLE_LEVELS } from "../mdSync";

test("the counter's bits are TR and TH, high for a one", () => {
  expect(mdSyncLevels(0)).toBe(0x00); // both lines low
  expect(mdSyncLevels(1)).toBe(0x20); // TR high, TH low
  expect(mdSyncLevels(2)).toBe(0x40); // TH high, TR low
  expect(mdSyncLevels(3)).toBe(0x60); // both high: the idle value the ROM's arming latches
  expect(GENMDDJ_SYNC_IDLE_LEVELS).toBe(0x60);
});

test("the counter wraps every four clocks", () => {
  expect(GENMDDJ_SYNC_COUNTER_MOD).toBe(4);
  expect(mdSyncLevels(4)).toBe(mdSyncLevels(0));
  expect(mdSyncLevels(-1)).toBe(mdSyncLevels(3));
});
