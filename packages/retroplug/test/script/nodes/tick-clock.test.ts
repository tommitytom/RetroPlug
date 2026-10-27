// A protocol node's clock (src/script/nodes/clock.ts) over the tick walk (src/ticks.ts, the TS twin of native
// PpqUtil::eachTick, whose own cases are test/dsp/each-tick.test.ts's): the counter carried across blocks, the same
// ticks however the blocks are cut, and a walk a callback throws out of leaving the counter where it was.
import { test, expect } from "../../../testing/harness";
import type { TickBlock } from "../../../src/ticks";
import { TickClock } from "../../../src/script/nodes/clock";
import type { Block } from "../../../src/graph/defineNode";

// 22050 frames @ 44100 Hz / 120 BPM = 0.5 s = exactly 1 beat → 24 ticks at 24 PPQN.
const tickBlock = (ppqStart: number, transport: boolean): TickBlock => ({ frames: 22050, sampleRate: 44100, tempo: 120, ppqStart, transport });
const block = (ppq: number, playing: boolean, frames = 22050): Block =>
  ({ frames, transport: { sampleRate: 44100, tempo: 120, ppq, playing } }) as Block;

test("a node's clock carries its counter across blocks, as the kernel's per-system counter did", () => {
  const clock = new TickClock();
  const ticks: number[] = [];
  const record = (t: number) => ticks.push(t);
  clock.begin(block(0, true));
  expect(clock.block).toEqual(tickBlock(0, true));
  clock.walk(24, record);
  clock.begin(block(1, true));
  clock.walk(24, record);
  expect(ticks).toEqual(Array.from({ length: 48 }, (_, i) => i));
  expect(clock.next).toBe(48);
  clock.begin(block(2, false));
  clock.walk(24, record);
  expect(ticks.length).toBe(48);
});

test("a node's clock in 1-frame blocks fires each tick once, at the same ticks as one long block", () => {
  const clock = new TickClock();
  const ticks: number[] = [];
  for (let f = 0; f < 22050; f++) {
    clock.begin(block(f / 22050, true, 1));
    clock.walk(24, (t) => ticks.push(t));
  }
  expect(ticks).toEqual(Array.from({ length: 24 }, (_, i) => i));
});

test("a walk a callback throws out of leaves the counter where it was", () => {
  const clock = new TickClock();
  clock.begin(block(0, true));
  expect(() =>
    clock.walk(24, (t) => {
      if (t === 3) throw new Error("stop");
    }),
  ).toThrow("stop");
  expect(clock.next).toBe(0);
});
