// The DSP pushSerialIn sink IN the render loop, driven by the real kernel. Host MIDI is staged
// globally; the kernel's midi-routing project behaviour fans it to systems, and each system's `mgb`
// role turns its routed bytes into serial input (mGB reads MIDI-over-serial, so raw serial == onMidi).
// The toggle is ROUTING PRESENCE — the native twin of routing.test.ts's two cases: with a routing
// role the chord reaches mGB and it sounds; with none it reaches no system and mGB stays silent. So
// pushSerialIn demonstrably reaches the live core, purely from the kernel structure. In-TS RMS.
import { test, expect } from "../testing/harness";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { SystemsStore } from "../src/systemsStore";
import { createRealBackend } from "../src/realBackend";

declare const __DSP_KERNEL_BUNDLE__: string;

// One note per channel (mGB's per-voice MIDI inputs) — a C-major chord.
const CHORD: number[][] = [
  [0x90, 60, 100],
  [0x91, 64, 100],
  [0x92, 67, 100],
];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a DSP pushSerialIn (mgb role) in the render loop flips the core silent ↔ audible via routing", () => {
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = new SystemsStore(createRealBackend()).loadMgb()!; // real mGB core in the shared Project
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  audio.renderAudio(1500); // warm up mGB

  const stageChord = () => CHORD.forEach((m) => audio.stageMidiIn(m));

  // No routing role -> staged MIDI reaches no system -> mGB gets nothing -> silent.
  expect(dsp.setSystems({ project: [], systems: [{ id, pipeline: [{ kind: "mgb", config: {} }] }] })).toBeTruthy();
  stageChord();
  const muted = rms(audio.renderAudio(1500));

  // Add midi-routing (SendToAll) -> the chord reaches mGB's serial via pushSerialIn -> it plays.
  expect(
    dsp.setSystems({
      project: [{ kind: "midi-routing", config: { mode: 0 } }],
      systems: [{ id, pipeline: [{ kind: "mgb", config: {} }] }],
    }),
  ).toBeTruthy();
  stageChord();
  const passed = rms(audio.renderAudio(1500));

  console.log(`[dsp-serial] muted=${muted.toFixed(5)} passed=${passed.toFixed(5)}`);
  expect(muted < 0.001).toBeTruthy(); // no routing -> the bytes reach no system -> silent
  expect(passed > 0.001).toBeTruthy(); // routing -> mgb pushes them into serialIn -> the core sounds
  expect(passed > muted).toBeTruthy();
});
