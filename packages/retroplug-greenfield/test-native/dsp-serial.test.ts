// The DSP pushSerialIn sink IN the render loop — the third foundational sink and the eachTick
// companion (doc-06: the LSDj MidiSync clock is just eachTick → pushSerialIn(0xF8)). A loaded
// translator script feeds RAW SERIAL bytes into the attached core's serial input, and those bytes
// drive its audio. Proven with a GATE script whose config.mute decides whether the host's mGB
// note bytes reach serialIn_: the same mGB flips silent ↔ audible purely from the DSP config, so
// pushSerialIn demonstrably reaches the live core. (For mGB, raw serial == onMidi — its role does
// no translation, so this is the audio-render-dsp gate rerouted through the serial sink.) In-TS
// RMS (no reaper).
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

// A gate: config.mute decides whether the note bytes are forwarded, byte-by-byte, into the
// system's serial input via pushSerialIn (instead of emitMidiOut). mGB reads MIDI-over-serial,
// so forwarding the raw bytes drives it exactly as onMidi would.
const SERIAL_GATE = `
var state = { mute: false };
function setConfig(json) { state.mute = !!JSON.parse(json).mute; }
function onBlock(input) {
  if (state.mute) return;
  for (var i = 0; i < input.midi.length; i++) {
    var m = input.midi[i];
    for (var b = 0; b < m.data.length; b++) pushSerialIn(m.frame, m.data[b]);
  }
}
`;

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a DSP pushSerialIn gate in the render loop flips the core silent ↔ audible via config", () => {
  const be = createRealBackend();
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = new SystemsStore(be).loadMgb()!; // real mGB core in the shared Project
  audio.renderAudio(1500); // warm up mGB (no DSP attached yet)

  expect(dsp.loadScript(dsp.compileScript(SERIAL_GATE)!)).toBeTruthy();
  expect(audio.dspAttach(id)).toBeTruthy();

  // mute=true → the DSP drops the chord → nothing reaches serialIn_ → silent.
  dsp.setConfig(enc.encode(JSON.stringify({ mute: true })));
  for (const m of CHORD) audio.sendDspMidi(m);
  const muted = rms(audio.renderAudio(1500));

  // mute=false → the DSP forwards the bytes into serialIn_ → mGB plays.
  dsp.setConfig(enc.encode(JSON.stringify({ mute: false })));
  for (const m of CHORD) audio.sendDspMidi(m);
  const passed = rms(audio.renderAudio(1500));

  console.log(`[dsp-serial] muted=${muted.toFixed(5)} passed=${passed.toFixed(5)}`);
  expect(muted < 0.001).toBeTruthy(); // the DSP gate blocked the serial bytes → silent
  expect(passed > 0.001).toBeTruthy(); // the DSP gate pushed them into serialIn_ → the core sounds
  expect(passed > muted).toBeTruthy();
});
