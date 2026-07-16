// The CLI Timeline builder is pure — build() flattens the fluent calls to a stable ms-sorted event list
// with no engine involved — so it's unit-testable here in the mock suite. (renderTimeline, which drives a
// real AudioDriver, is covered natively in test-native/cli-timeline.test.ts.)
import { test, expect } from "../../testing/harness";
import { Timeline, Button } from "../../cli/timeline";

test("build() stable-sorts events by ms; insertion order breaks ties", () => {
  const evs = new Timeline()
    .midi(500, [0x90, 62, 100])
    .midi(0, [0x90, 60, 100])
    .midi(0, [0x91, 64, 100]) // same ms as the previous — must keep insertion order
    .build();
  expect(evs.map((e) => e.ms)).toEqual([0, 0, 500]);
  expect((evs[0] as { bytes: number[] }).bytes).toEqual([0x90, 60, 100]);
  expect((evs[1] as { bytes: number[] }).bytes).toEqual([0x91, 64, 100]);
});

test("note expands to noteOn + noteOff with the right bytes and timing", () => {
  const evs = new Timeline().note(100, 60, { durationMs: 400 }).build();
  expect(evs.length).toBe(2);
  expect(evs[0]).toEqual({ ms: 100, kind: "midi", bytes: [0x90, 60, 100] });
  expect(evs[1]).toEqual({ ms: 500, kind: "midi", bytes: [0x80, 60, 0] });
});

test("channel is 1-based → the status low nibble; velocity honored", () => {
  const on = new Timeline().noteOn(0, 67, { channel: 2, velocity: 64 }).build()[0] as { bytes: number[] };
  expect(on.bytes).toEqual([0x91, 67, 64]); // channel 2 → 0x91
  const off = new Timeline().noteOff(0, 67, { channel: 16 }).build()[0] as { bytes: number[] };
  expect(off.bytes).toEqual([0x8f, 67, 0]); // channel 16 → 0x8f
});

test("tap expands to a down then an up at ms + holdMs (default 50)", () => {
  const evs = new Timeline().tap(200, 3, Button.A, { holdMs: 80 }).build();
  expect(evs).toEqual([
    { ms: 200, kind: "press", system: 3, button: 4, down: true },
    { ms: 280, kind: "press", system: 3, button: 4, down: false },
  ]);
  expect(new Timeline().tap(0, 1, Button.Start).build()[1].ms).toBe(50); // default hold
});

test("at() records a scheduled callback that sorts by ms; build() never invokes it", () => {
  const seen: string[] = [];
  const evs = new Timeline()
    .midi(0, [0x90, 60, 100])
    .at(200, () => seen.push("probe"))
    .midi(100, [0x80, 60, 0])
    .build();
  expect(evs.map((e) => e.kind)).toEqual(["midi", "midi", "at"]);
  expect(evs.map((e) => e.ms)).toEqual([0, 100, 200]);
  expect(seen).toEqual([]); // build() is pure — the callback is carried, not run
  (evs[2] as unknown as { fn: () => void }).fn();
  expect(seen).toEqual(["probe"]);
});

test("raw midi / bpm / transport / screenshot pass through as typed events", () => {
  const evs = new Timeline()
    .midi(0, [0xb0, 1, 64])
    .bpm(10, 140)
    .transport(20, true)
    .screenshot(30, 7, "/tmp/x.png")
    .build();
  expect(evs).toEqual([
    { ms: 0, kind: "midi", bytes: [0xb0, 1, 64] },
    { ms: 10, kind: "bpm", bpm: 140 },
    { ms: 20, kind: "transport", running: true },
    { ms: 30, kind: "screenshot", system: 7, path: "/tmp/x.png" },
  ]);
});
