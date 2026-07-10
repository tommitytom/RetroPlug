// walkTicks — the TS twin of native PpqUtil::eachTick. Proves the drift-free 24-PPQN clock: two
// consecutive half-second blocks at 44100/120 each span exactly 1 beat = 24 ticks, the boundary
// tick fires exactly once (contiguous 0..47, no double/miss), and a stopped transport emits none.
import { test, expect } from "../../testing/harness";
import { walkTicks, type BlockInfo } from "../../src/dspKernel";

// 22050 frames @ 44100 Hz / 120 BPM = 0.5 s = exactly 1 beat → 24 ticks at 24 PPQN.
const block = (ppqStart: number, transport: boolean): BlockInfo => ({
  frames: 22050,
  sampleRate: 44100,
  tempo: 120,
  ppqStart,
  transport,
});

test("walkTicks emits a drift-free 24-PPQN clock (24 / 24 / 0 across two blocks + a stopped control)", () => {
  // Block 1 (beat 0): ticks 0..23, first at sample offset 0.
  const b1: { t: number; o: number }[] = [];
  let nextTick = walkTicks(block(0, true), 24, 0, (t, o) => b1.push({ t, o }));
  expect(b1.length).toBe(24);
  expect(b1[0]).toEqual({ t: 0, o: 0 });
  expect(b1[23].t).toBe(23);
  expect(nextTick).toBe(24);

  // Block 2 (beat 1): ticks 24..47 — the boundary tick 24 fires exactly once (drift-free).
  const b2: number[] = [];
  nextTick = walkTicks(block(1.0, true), 24, nextTick, (t) => b2.push(t));
  expect(b2.length).toBe(24);
  expect(b2[0]).toBe(24);
  expect(b2[23]).toBe(47);
  expect(nextTick).toBe(48);

  // Control: transport stopped → no ticks, nextTick unchanged.
  const stopped: number[] = [];
  const after = walkTicks(block(2.0, false), 24, nextTick, (t) => stopped.push(t));
  expect(stopped.length).toBe(0);
  expect(after).toBe(48);
});
