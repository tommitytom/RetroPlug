// The DSP-side JS runtime over the REAL native host: a second, bare QuickJS context that runs
// a translator script per block, fed only by bytes. Proves the whole seam (plans/03) — the
// script crosses as QuickJS bytecode (compiled on a separate context, so the DSP side never
// re-parses source), config crosses as a byte blob overwriting pre-alloc slots, per-block MIDI
// crosses as structured bytes, and bytecode hot-reload swaps behavior. Observable outcomes only.
//
// One host process per file → one native DspRuntime singleton shared across cases; each case
// loadScript()s first, which re-runs the global code and resets state, so cases are independent.
import { test, expect } from "../testing/harness";
import { createDspRuntime } from "../src/dspRuntime";

const enc = new TextEncoder();

// A note-on transposer: config sets the amount; everything non-note-on passes through.
const TRANSPOSE_SCRIPT = `
var state = { transpose: 0 };                     // allocated ONCE at load
function setConfig(json) { state.transpose = JSON.parse(json).transpose | 0; }
function onBlock(input) {
  for (var i = 0; i < input.midi.length; i++) {
    var ev = input.midi[i], s = ev.data[0];
    if ((s & 0xf0) === 0x90) emitMidiOut(ev.frame, [s, (ev.data[1] + state.transpose) & 0x7f, ev.data[2]]);
    else emitMidiOut(ev.frame, ev.data);
  }
}
`;

// Hot-reload target: drop note-offs (0x80), pass everything else through.
const DROP_NOTE_OFF_SCRIPT = `
function setConfig(json) {}
function onBlock(input) {
  for (var i = 0; i < input.midi.length; i++) {
    var ev = input.midi[i];
    if ((ev.data[0] & 0xf0) !== 0x80) emitMidiOut(ev.frame, ev.data);
  }
}
`;

const block = { frames: 512, sampleRate: 44100, tempo: 120, ppqPosBlockStart: 0, transportPlaying: true };
const noteOn = (n: number, v = 100, frame = 0) => ({ frame, data: new Uint8Array([0x90, n, v]) });

test("compile → load → transpose a note-on by the configured amount (bytecode seam)", () => {
  const dsp = createDspRuntime();
  const bc = dsp.compileScript(TRANSPOSE_SCRIPT)!;
  expect(bc.length > 0).toBeTruthy(); // real bytecode bytes came back
  expect(dsp.loadScript(bc)).toBeTruthy();

  expect(dsp.setConfig(enc.encode(JSON.stringify({ transpose: 12 })))).toBeTruthy();
  const out = dsp.runBlock([noteOn(60)], block);
  expect(out.length).toBe(1);
  expect(out[0].frame).toBe(0);
  expect(Array.from(out[0].data)).toEqual([0x90, 72, 100]); // 60 + 12, emitted from the DSP heap
});

test("setConfig overwrites the live pre-alloc slot (5 then 0)", () => {
  const dsp = createDspRuntime();
  dsp.loadScript(dsp.compileScript(TRANSPOSE_SCRIPT)!);

  dsp.setConfig(enc.encode(JSON.stringify({ transpose: 5 })));
  expect(Array.from(dsp.runBlock([noteOn(60)], block)[0].data)).toEqual([0x90, 65, 100]);

  dsp.setConfig(enc.encode(JSON.stringify({ transpose: 0 })));
  expect(Array.from(dsp.runBlock([noteOn(60)], block)[0].data)).toEqual([0x90, 60, 100]); // passthrough
});

test("non-note events pass through unchanged (frame preserved)", () => {
  const dsp = createDspRuntime();
  dsp.loadScript(dsp.compileScript(TRANSPOSE_SCRIPT)!);
  dsp.setConfig(enc.encode(JSON.stringify({ transpose: 12 })));

  const out = dsp.runBlock([{ frame: 8, data: new Uint8Array([0xb0, 7, 100]) }], block); // CC
  expect(out.length).toBe(1);
  expect(out[0].frame).toBe(8);
  expect(Array.from(out[0].data)).toEqual([0xb0, 7, 100]);
});

test("hot-reload: loading different bytecode swaps behavior (drop note-offs)", () => {
  const dsp = createDspRuntime();
  dsp.loadScript(dsp.compileScript(TRANSPOSE_SCRIPT)!);
  // Under the transposer a note-off (0x80) passes through untouched.
  expect(dsp.runBlock([{ frame: 0, data: new Uint8Array([0x80, 60, 0]) }], block).length).toBe(1);

  expect(dsp.loadScript(dsp.compileScript(DROP_NOTE_OFF_SCRIPT)!)).toBeTruthy();
  const after = dsp.runBlock(
    [
      { frame: 0, data: new Uint8Array([0x80, 60, 0]) }, // note-off → dropped by the new script
      noteOn(62, 90, 4), // note-on → kept
    ],
    block,
  );
  expect(after.length).toBe(1);
  expect(Array.from(after[0].data)).toEqual([0x90, 62, 90]);
});

test("a garbage source fails to compile → null bytecode", () => {
  const dsp = createDspRuntime();
  expect(dsp.compileScript("function ( { this is not js")).toBe(null);
});
