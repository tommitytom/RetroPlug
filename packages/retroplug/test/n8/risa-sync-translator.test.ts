// Guards the pure-TS RisaSyncTranslator (src/n8/risaSyncTranslator.ts): MIDI clock/transport -> risa
// arm/clock/stop bytes. Mirrors the former native gtest (packages/native/test/n8/Edio.test.cpp's
// RisaSyncTranslator cases) byte-for-byte, and the arm packets are the golden values from the role test
// test/dsp/risa-sync.test.ts (top -> F9 52 00 00 00; ppq 64 -> F9 52 01 00 00; 23-not-24 clocks after start).
import { test, expect } from "../../testing/harness";
import { RisaSyncTranslator } from "../../src/n8/risaSyncTranslator";

// Feed one MIDI message into the translator, appending any risa output to `out` (NOT cleared).
function feed(t: RisaSyncTranslator, msg: number[], out: number[]): void {
  t.onMessage(msg, out);
}
const countByte = (v: number[], b: number): number => v.filter((x) => x === b).length;

test("armPacket matches the risaSync.ts locate mapping (golden values)", () => {
  expect(RisaSyncTranslator.armPacket(0)).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00]); // top
  expect(RisaSyncTranslator.armPacket(1536)).toEqual([0xf9, 0x52, 0x01, 0x00, 0x00]); // ppq 64
  expect(RisaSyncTranslator.armPacket(120)).toEqual([0xf9, 0x52, 0x00, 0x01, 0x18]); // ppq 5 -> tick 24
  expect(RisaSyncTranslator.armPacket(95)).toEqual([0xf9, 0x52, 0x00, 0x00, 0x5f]); // last grid position
});

test("Start arms from the top then streams 23 clocks", () => {
  const t = new RisaSyncTranslator();
  let out: number[] = [];

  feed(t, [0xfa], out); // MIDI Start -> arm at the top (barrier) + FA
  expect(out).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00, 0xfa]);
  expect(t.playing()).toBe(true);

  // 24 clocks in the first quarter, but risa primes the armed clock itself -> only 23 F8 emitted.
  out = [];
  for (let i = 0; i < 24; i++) feed(t, [0xf8], out);
  expect(out).toEqual(new Array(23).fill(0xf8));

  // A second quarter re-arms nothing and streams all 24.
  out = [];
  for (let i = 0; i < 24; i++) feed(t, [0xf8], out);
  expect(out).toEqual(new Array(24).fill(0xf8));
});

test("Stop emits FC and gates further clocks", () => {
  const t = new RisaSyncTranslator();
  const out: number[] = [];
  feed(t, [0xfa], out);
  for (let i = 0; i < 5; i++) feed(t, [0xf8], out);

  const afterStop: number[] = [];
  feed(t, [0xfc], afterStop); // Stop
  expect(afterStop).toEqual([0xfc]);
  expect(t.playing()).toBe(false);

  const afterGate: number[] = [];
  for (let i = 0; i < 8; i++) feed(t, [0xf8], afterGate); // clocks after a stop are ignored
  expect(afterGate).toEqual([]);
});

test("Continue arms from the Song Position", () => {
  const t = new RisaSyncTranslator();
  const out: number[] = [];

  feed(t, [0xf2, 0x00, 0x02], out); // SPP -> 256 sixteenths = 1536 clocks (ppq 64)
  expect(out).toEqual([]); // SPP itself emits nothing
  expect(t.getAbsoluteClock()).toBe(1536);

  feed(t, [0xfb], out); // Continue -> arm at the current position
  expect(out).toEqual([0xf9, 0x52, 0x01, 0x00, 0x00, 0xfa]);
  expect(t.playing()).toBe(true);
});

test("ignores non-transport MIDI and stopped clocks", () => {
  const t = new RisaSyncTranslator();
  const out: number[] = [];

  feed(t, [0x90, 0x3c, 0x7f], out); // note-on
  feed(t, [0xb0, 0x07, 0x64], out); // CC volume
  feed(t, [0xf8], out); // clock while stopped
  expect(out).toEqual([]);
  expect(t.playing()).toBe(false);

  // After a start, note-on still emits nothing but clocks flow (first/armed suppressed).
  feed(t, [0xfa], out);
  const clks: number[] = [];
  feed(t, [0x90, 0x40, 0x7f], clks);
  expect(clks).toEqual([]);
  for (let i = 0; i < 3; i++) feed(t, [0xf8], clks);
  expect(countByte(clks, 0xf8)).toBe(2); // 3 clocks, first (armed) suppressed
});
