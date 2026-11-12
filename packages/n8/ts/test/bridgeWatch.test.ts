// The N8 bridges' rule for "the cart is gone" (failureWatch, packages/n8/ts/bridge.ts, which n8-bridge and risa's
// n8-sync share): a RUN of failed FIFO writes, not two isolated ones around a quiet stretch of MIDI, which is what a
// rule counting failures over wall time would exit on, mid-set.
import { test, expect } from "@retroplug/testing";
import type { CliHost } from "@retroplug/core/cliTool";
import { failureWatch, runBridge } from "../bridge";

test("two failures more than the window apart are a new run, not a fatal one", () => {
  // minFailures 1: without the restart after a gap, the second failure would end a run that has lasted 5 s.
  const w = failureWatch(3000, 1);
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

// runBridge over a fake host: the poll loop's timer callbacks are captured, the clock is the test's, and every FIFO
// write answers as the test says. What it holds is what the loop does with a failed write: counts it, stays up for one
// hiccup, and on a run of them closes the session and exits once, however many messages the tick still has queued.
function bridgeRig(): {
  tick: (at: number, messages: number) => void;
  failWrites: (on: boolean) => void;
  sent: string[];
  exits: number[];
  errors: string[];
  statuses: () => string[];
} {
  const sent: string[] = [];
  const exits: number[] = [];
  const errors: string[] = [];
  let failing = false;
  let now = 0;
  let pending = 0;
  const call = (method: string): unknown => {
    sent.push(method);
    if (method === "n8ListPorts") return [{ port: "fake", isN8: true }];
    if (method === "n8Open") return { status: "ok", deviceStatus: 0, message: "", value: 4 };
    if (method === "n8Close") return true;
    if (method === "n8FifoWR" && failing) return { status: "linkDown", deviceStatus: 0, message: "link down" };
    return { status: "ok", deviceStatus: 0, message: "", value: 0 };
  };
  const host = {
    devices: {
      rpc: call,
      midi: () => ({
        open: () => true,
        listInputs: () => [],
        poll: () => {
          const out = Array.from({ length: pending }, () => new Uint8Array([0x90, 0x3c, 0x7f]));
          pending = 0;
          return out;
        },
      }),
    },
    process: { keepAlive: () => {}, exit: (code: number) => void exits.push(code) },
  } as unknown as CliHost;

  const timers: (() => void)[] = [];
  const real = (globalThis as unknown as { setInterval: unknown }).setInterval;
  const realLog = console.log;
  const realError = console.error;
  (globalThis as unknown as { setInterval: unknown }).setInterval = (f: () => void) => timers.push(f);
  console.log = () => {};
  console.error = (m: string) => void errors.push(m);
  try {
    runBridge(host, [], "test", (bytes, out) => void out.push(...bytes), () => now);
  } finally {
    (globalThis as unknown as { setInterval: unknown }).setInterval = real;
    console.log = realLog;
    console.error = realError;
  }
  return {
    tick: (at, messages) => {
      now = at;
      pending = messages;
      const e = console.error;
      console.error = (m: string) => void errors.push(m);
      try {
        timers[0]();
      } finally {
        console.error = e;
      }
    },
    failWrites: (on) => (failing = on),
    sent,
    exits,
    errors,
    statuses: () => {
      const lines: string[] = [];
      const l = console.log;
      console.log = (m: string) => void lines.push(m);
      try {
        timers[1]();
      } finally {
        console.log = l;
      }
      return lines;
    },
  };
}

test("one failed write is counted and logged, and the bridge stays up", () => {
  const rig = bridgeRig();
  rig.failWrites(true);
  rig.tick(0, 1);
  expect(rig.exits).toEqual([]);
  expect(rig.errors.length).toBe(1);
  expect(rig.errors[0].includes("a write failed (1 so far): link down")).toBe(true);
  rig.failWrites(false);
  rig.tick(10, 2);
  expect(rig.statuses()).toEqual(["forwarded 2 messages / 6 bytes, 1 failed"]);
});

test("a sustained run of failures closes the session and exits once, not once per message left in the tick", () => {
  const rig = bridgeRig();
  rig.failWrites(true);
  for (const t of [0, 800, 1600, 2400]) rig.tick(t, 1);
  expect(rig.exits).toEqual([]);
  rig.tick(3200, 4); // the first of these is the fatal one; three more are queued behind it in the same poll
  rig.tick(3300, 2);
  expect(rig.exits).toEqual([1]);
  expect(rig.sent.filter((m) => m === "n8Close")).toEqual(["n8Close"]);
  expect(rig.errors.filter((m) => m.includes("the N8 is gone")).length).toBe(1);
});

test("a write that succeeds between failures keeps the bridge alive", () => {
  const rig = bridgeRig();
  rig.failWrites(true);
  for (const t of [0, 800, 1600, 2400]) rig.tick(t, 1);
  rig.failWrites(false);
  rig.tick(2800, 1);
  rig.failWrites(true);
  rig.tick(3600, 1);
  expect(rig.exits).toEqual([]);
});
