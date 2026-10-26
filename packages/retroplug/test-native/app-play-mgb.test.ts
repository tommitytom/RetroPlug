// The composition root, proven for mGB: a real project assembled from the app-layer STORES
// (ProjectStore/SystemsStore) + the role registry + the DSP kernel plays real audio driven
// entirely THROUGH the stores. loadMgb() attaches the `mgb` feature role via the ROM provider;
// the graph the store publishes carries the projected kernel structure; a staged MIDI chord -
// routed by the projected midi-routing role and serialized by the `mgb` role - makes the core
// sing. Same proof as audio-render.test.ts, but reached via the stores instead of a hand-set
// pipeline, so it exercises the real store→projection→DSP path a host will drive. (One test per
// file: whole-mix RMS needs an isolated native engine.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

// mGB listens on a MIDI channel per pulse voice; a C-major chord is one note per channel.
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

test("mGB plays a MIDI chord driven through the app stores (composition root)", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const audio = createAudioDriver();

  // Embedded mGB → the ROM provider attaches the `mgb` role; the store publishes the projected structure.
  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");
  // The STORE attached the feature role from the ROM identity — not a hand-written pipeline.
  expect(project.systems.view()[0].roles.map((r) => r.kind).includes("mgb")).toBeTruthy();

  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init (discarded)

  const idle = rms(audio.renderAudio(500)); // no MIDI staged → near-silent baseline
  CHORD.forEach((m) => audio.stageMidiIn(m)); // host MIDI → projected midi-routing → mgb serial
  const playing = rms(audio.renderAudio(1500)); // the chord rings

  console.log(`[app-play-mgb] idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.01).toBeTruthy(); // control: silent before the notes
  expect(playing > 0.001).toBeTruthy(); // the chord is audible
  expect(playing > idle).toBeTruthy(); // driven by the MIDI through the store→DSP path
});
