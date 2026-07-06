// The DSP runtime IN the render loop — the doc-06 bridge. A loaded translator script runs per
// block and its emitMidiOut output drives the real core's audio (host MIDI → DSP script → mGB →
// sound). Proven with a GATE script whose config.mute decides whether note-ons reach the core:
// the same mGB flips silent ↔ audible purely from the DSP config, so the DSP demonstrably sits
// in the MIDI→audio path. In-TS RMS (no reaper).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { SystemsStore } from "../src/systemsStore";

const enc = new TextEncoder();

// One note per channel (mGB's per-voice MIDI inputs) — a C-major chord.
const CHORD: number[][] = [
  [0x90, 60, 100],
  [0x91, 64, 100],
  [0x92, 67, 100],
];

// A gate: config.mute decides whether note-ons pass through to the core.
const GATE = `
var state = { mute: false };
function setConfig(json) { state.mute = !!JSON.parse(json).mute; }
function onBlock(input) {
  if (state.mute) return;
  for (var i = 0; i < input.midi.length; i++) emitMidiOut(input.midi[i].frame, input.midi[i].data);
}
`;

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a DSP gate script in the render loop flips the core silent ↔ audible via config", () => {
  const be = createRealBackend();
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = new SystemsStore(be).loadMgb()!; // real mGB core in the shared Project
  audio.renderAudio(1500); // warm up mGB (no DSP attached yet)

  expect(dsp.loadScript(dsp.compileScript(GATE)!)).toBeTruthy();
  expect(audio.dspAttach(id)).toBeTruthy();

  // mute=true → the DSP drops the chord → mGB never receives it → silent.
  dsp.setConfig(enc.encode(JSON.stringify({ mute: true })));
  for (const m of CHORD) audio.sendDspMidi(m);
  const muted = rms(audio.renderAudio(1500));

  // mute=false → the DSP forwards the chord → mGB plays.
  dsp.setConfig(enc.encode(JSON.stringify({ mute: false })));
  for (const m of CHORD) audio.sendDspMidi(m);
  const passed = rms(audio.renderAudio(1500));

  console.log(`[dsp-render] muted=${muted.toFixed(5)} passed=${passed.toFixed(5)}`);
  expect(muted < 0.001).toBeTruthy(); // the DSP gate blocked the notes → silent
  expect(passed > 0.001).toBeTruthy(); // the DSP gate passed them → the core sounds
  expect(passed > muted).toBeTruthy();
});
