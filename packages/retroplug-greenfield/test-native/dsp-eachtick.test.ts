// The DSP eachTick primitive — doc-06's drift-exact PPQ iterator, bound into the DSP context.
// A clock script emits a 24-PPQN MIDI clock (0xF8) per tick via eachTick + emitMidiOut; the
// output is observed in the returned MIDI (no system/audio needed). Proves exact tick counts
// AND drift-free accumulation across a block boundary (a tick lands on the edge yet fires once).
import { test, expect } from "../testing/harness";
import { createDspRuntime } from "../src/dspRuntime";

// 22050 frames @ 44100 Hz / 120 BPM = 0.5 s = exactly 1 beat → 24 ticks at 24 PPQN.
const CLOCK = `
function onBlock(input) {
  eachTick(24, function(tick, off) { emitMidiOut(off, [0xF8]); });
}
`;

const block = (ppq: number, playing: boolean) => ({
  frames: 22050,
  sampleRate: 44100,
  tempo: 120,
  ppqPosBlockStart: ppq,
  transportPlaying: playing,
});

const allClocks = (out: { frame: number; data: Uint8Array }[]): boolean =>
  out.every((e) => e.data.length === 1 && e.data[0] === 0xf8);

test("eachTick emits a drift-free 24-PPQN clock (24 / 24 / 0 across two blocks + a stopped control)", () => {
  const dsp = createDspRuntime();
  expect(dsp.loadScript(dsp.compileScript(CLOCK)!)).toBeTruthy(); // resets nextTick

  // Block 1 (beat 0): ticks 0..23.
  const b1 = dsp.runBlock([], block(0, true));
  expect(b1.length).toBe(24);
  expect(allClocks(b1)).toBeTruthy();

  // Block 2 (beat 1): ticks 24..47 — the boundary tick fires exactly once (drift-free).
  const b2 = dsp.runBlock([], block(1.0, true));
  expect(b2.length).toBe(24);
  expect(allClocks(b2)).toBeTruthy();

  // Control: transport stopped → eachTick no-ops → no clock.
  const stopped = dsp.runBlock([], block(2.0, false));
  expect(stopped.length).toBe(0);
});
