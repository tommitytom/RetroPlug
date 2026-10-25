// The audio-routing seam: the project's `audioRouting` mode reaches native audio as the output wiring of
// the graph the project publishes (graph/buildGraph.ts). This proves the seam is wired end-to-end - every
// mode plans, an unknown one is refused, and playback survives a mode switch (a single system still lands
// on its pair). Per-pair SEPARATION is buildGraph's wiring tables and the parity routing cases; the
// test-host render is stereo, so every mode collapses to one pair here. (One test per file: whole-mix RMS
// needs an isolated native engine.)
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

  // The seam, through the store (the real UI path: the menu cycler calls this): every mode plans, and an
  // unknown one is refused before it reaches native. ChannelSplit on this GB fans its stems, which the
  // stereo host is too narrow for, so it falls back to Stereo; the pin splits are inert on a GB.
  for (const mode of ["stereo", "twoPerInstance", "onePerInstance", "channelSplit", "pinSplit", "stereoPinSplit"] as const)
    expect(project.setAudioRouting(mode)).toBeTruthy();
  expect(project.setAudioRouting("bogus" as never)).toBeFalsy();
  expect(project.setAudioRouting("twoPerInstance")).toBeTruthy();

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
