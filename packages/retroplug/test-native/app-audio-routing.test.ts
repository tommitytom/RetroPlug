// The audio-routing seam: the project's `audioRouting` mode now reaches native (the block runner's
// MultiOutRouter), through the same command path as setBpm/setTransport. This proves the seam is
// wired end-to-end — the RPC is accepted for the three valid modes, rejected out of range, and
// playback survives a mode switch (a single system still lands on its pair). Per-pair SEPARATION
// needs the plugin's 8 outputs + a multi-out capture and is the user's / a DAW fixture's check;
// the test-host render is stereo, so every mode collapses to one pair here. (One test per file:
// whole-mix RMS needs an isolated native Project.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

const CHORD: number[][] = [
  [0x90, 60, 100], // ch0 C4
  [0x91, 64, 100], // ch1 E4
  [0x92, 67, 100], // ch2 G4
];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("audioRouting reaches native and playback survives a mode switch", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  // The seam: valid modes accepted (Stereo / TwoPerInstance / OnePerInstance / ChannelSplit), out of
  // range rejected. (Per-pair SEPARATION for ChannelSplit is a native-Catch2 render check — this
  // test-host capture is stereo, so it can only prove acceptance here; see ChannelSplit.test.cpp.)
  expect(be.setAudioRouting(0)).toBeTruthy();
  expect(be.setAudioRouting(1)).toBeTruthy();
  expect(be.setAudioRouting(2)).toBeTruthy();
  expect(be.setAudioRouting(3)).toBeTruthy(); // ChannelSplit (1 GB → 8 outs) — now accepted
  expect(be.setAudioRouting(4)).toBeFalsy(); // > ChannelSplit → rejected
  // Through the store (the real UI path — the menu cycler calls this), which pushes to native.
  expect(project.setAudioRouting(1)).toBeTruthy();

  // Playback survives the switch: under TwoPerInstance the single system routes to its own pair
  // (slot 0 → pair 0), which the stereo capture still hears.
  audio.renderAudio(1500); // warm up (discarded)
  const idle = rms(audio.renderAudio(500)); // no MIDI → baseline
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const playing = rms(audio.renderAudio(1500)); // the chord rings under TwoPerInstance

  console.log(`[app-audio-routing] idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.01).toBeTruthy();
  expect(playing > 0.001).toBeTruthy(); // audible after a routing change
  expect(playing > idle).toBeTruthy();
});
