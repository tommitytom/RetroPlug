// The N8 bridges' rule for "the cart is gone" (failureWatch, packages/n8/ts/bridge.ts, which n8-bridge and risa's
// n8-sync share): a RUN of failed FIFO writes, not two isolated ones around a quiet stretch of MIDI, which is what a
// rule counting failures over wall time would exit on, mid-set.
import { test, expect } from "@retroplug/testing";
import { failureWatch } from "../bridge";

test("two hiccups either side of a quiet stretch are not fatal", () => {
  const w = failureWatch(3000, 5);
  expect(w.failed(1_000)).toBe(false);
  expect(w.failed(6_000)).toBe(false); // 5 s later, nothing written between: a new run
});

test("a sustained run of failures is fatal once it has lasted the window and held enough of them", () => {
  const w = failureWatch(3000, 5);
  const verdicts = [0, 800, 1600, 2400, 3200].map((t) => w.failed(10_000 + t));
  expect(verdicts).toEqual([false, false, false, false, true]);
});

test("a long window with too few failures is not fatal", () => {
  const w = failureWatch(3000, 5);
  expect([0, 2900, 5800].map((t) => w.failed(t))).toEqual([false, false, false]);
});

test("a success in between ends the run", () => {
  const w = failureWatch(3000, 5);
  for (const t of [0, 500, 1000, 1500]) w.failed(t);
  w.succeeded();
  expect(w.failed(3600)).toBe(false);
});

test("a burst, a long quiet stretch, then one more failure is a new run, not five in a row", () => {
  // Without the run restarting after a gap, the burst's four and the late one add up to five failures spanning ten
  // seconds: fatal, for a cart that only hiccuped twice.
  const w = failureWatch(3000, 5);
  for (const t of [0, 100, 200, 300]) w.failed(t);
  expect(w.failed(10_000)).toBe(false);
});
