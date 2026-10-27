// The tick walk (src/ticks.ts, the TS twin of native PpqUtil::eachTick) and a protocol node's clock over it
// (src/script/nodes/clock.ts). The expectations of test/dsp/each-tick.test.ts, which reads the walk through the kernel's
// re-export, here through its own module and through the clock a node keeps: a drift-free 24-PPQN clock, the boundary
// tick fired exactly once, a stopped transport firing none.
import { test, expect } from "../../../testing/harness";
import { walkTicks, type TickBlock } from "../../../src/ticks";
import { walkTicks as kernelWalkTicks } from "../../../src/dspKernel";
import { TickClock } from "../../../src/script/nodes/clock";
import type { Block } from "../../../src/graph/defineNode";

// 22050 frames @ 44100 Hz / 120 BPM = 0.5 s = exactly 1 beat → 24 ticks at 24 PPQN.
const tickBlock = (ppqStart: number, transport: boolean): TickBlock => ({ frames: 22050, sampleRate: 44100, tempo: 120, ppqStart, transport });
const block = (ppq: number, playing: boolean, frames = 22050): Block =>
  ({ frames, transport: { sampleRate: 44100, tempo: 120, ppq, playing } }) as Block;

test("the kernel's walkTicks is this one", () => {
  expect(kernelWalkTicks).toBe(walkTicks);
});

test("walkTicks emits a drift-free 24-PPQN clock (24 / 24 / 0 across two blocks + a stopped control)", () => {
  const b1: { t: number; o: number }[] = [];
  let nextTick = walkTicks(tickBlock(0, true), 24, 0, (t, o) => b1.push({ t, o }));
  expect(b1.length).toBe(24);
  expect(b1[0]).toEqual({ t: 0, o: 0 });
  expect(b1[23].t).toBe(23);
  expect(nextTick).toBe(24);

  const b2: number[] = [];
  nextTick = walkTicks(tickBlock(1.0, true), 24, nextTick, (t) => b2.push(t));
  expect(b2.length).toBe(24);
  expect(b2[0]).toBe(24);
  expect(b2[23]).toBe(47);
  expect(nextTick).toBe(48);

  const stopped: number[] = [];
  expect(walkTicks(tickBlock(2.0, false), 24, nextTick, (t) => stopped.push(t))).toBe(48);
  expect(stopped.length).toBe(0);
});

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
