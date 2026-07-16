// The per-channel pull path (renderAudioPerChannel), proven end-to-end through the real host + the
// app stores: an mGB chord rendered as its FOUR Game Boy channels (Pulse 1, Pulse 2, Wave, Noise)
// instead of the mix. Same store→projection→DSP drive as app-play-mgb, but the final render isolates
// each channel. Guards the RPC shape + marshaling and — crucially — real per-channel SEPARATION: a
// 3-note chord on the pulse voices lights the pulse channels while Noise stays quiet, which a mix
// duplicated four ways could never show. spec/10 §5.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

// One note per mGB voice (ch0→Pulse1, ch1→Pulse2, ch2→Wave); Noise (ch3) is left unplayed.
const CHORD: number[][] = [
  [0x90, 60, 100], // C4
  [0x91, 64, 100], // E4
  [0x92, 67, 100], // G4
];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("mGB renders as four isolated Game Boy channels (renderAudioPerChannel)", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // boot + firmware init (discarded)

  // Idle baseline: four channel streams (SameBoy's channelLayout()), each near-silent.
  const idle = audio.renderAudioPerChannel(id, 500);
  expect(idle.length).toBe(4); // Pulse 1, Pulse 2, Wave, Noise
  const frames = idle[0].length;
  expect(frames > 0).toBeTruthy();
  idle.forEach((ch) => expect(ch.length).toBe(frames)); // equal-length interleaved-stereo streams
  expect(Math.max(...idle.map(rms)) < 0.01).toBeTruthy();

  // Play the chord and isolate the channels.
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const play = audio.renderAudioPerChannel(id, 1500);
  expect(play.length).toBe(4);
  const [pulse1, pulse2, wave, noise] = play.map(rms);
  console.log(`[mgb-channels] p1=${pulse1.toFixed(5)} p2=${pulse2.toFixed(5)} wave=${wave.toFixed(5)} noise=${noise.toFixed(5)}`);

  // Signal flows through the per-channel path…
  expect(pulse1 > 0.001).toBeTruthy();
  // …and it's genuinely SEPARATED: the pulse voices carry the notes while the unplayed Noise channel
  // stays quiet. A mix copied into four lanes would make every channel equally loud.
  expect(pulse1 > noise * 4).toBeTruthy();
  expect(pulse2 > noise * 4).toBeTruthy();

  // Unknown system id → no streams (the null-return path).
  expect(audio.renderAudioPerChannel(999999, 100).length).toBe(0);
});
