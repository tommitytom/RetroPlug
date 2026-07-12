// The NES per-channel pull path (renderAudioPerChannel), proven end-to-end through the real Mesen core +
// the app stores: n8-midi's ch1 note, rendered as the two 2A03 "stereo-mod" pins — Pulse (Square1+Square2)
// and TND (DMC/Triangle/Noise) — plus the lumped Expansion term, instead of the mix. Guards the RPC shape
// + the NesSoundMixer pin tap and — crucially — real SEPARATION: a ch1 pulse note lights the Pulse pin
// while TND + Expansion stay quiet, which a mix copied three ways could never show. The pins are MONO, so
// each interleaved-stereo buffer's right lane is silent. spec/10 §5.
//
// Uses `adopt` (not loadRom): the mesen role's channelExportMode arms capture at CONSTRUCT (onActivate);
// setRoleConfig is the live path and would not engage it. adopt is quiet, so the store→DSP projection is
// driven by hand (syncDspFromStore), matching how ProjectStore.load re-projects after its adopt.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const NOTE_ON_CH1 = [0x90, 60, 100]; // ch1 NoteOn C4 → APU Pulse1 → the Pulse pin

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

// RMS of just the RIGHT lane of an interleaved-stereo buffer (odd indices).
const rightRms = (a: Float32Array): number => {
  let s = 0, n = 0;
  for (let i = 1; i < a.length; i += 2) { s += a[i] * a[i]; n++; }
  return n ? Math.sqrt(s / n) : 0;
};

test("NES renders its stereo-mod pins (renderAudioPerChannel): Pulse rings, TND/Expansion stay quiet", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log(`# SKIP app-play-nes-channels: no ROM at ${NES}`); return; }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  // Construct-time StereoModPins (channelExportMode 1) + the host-MIDI role. adopt is quiet → project by hand.
  project.systems.adopt({
    romPath: NES,
    roles: [
      { kind: "nes-n8-midi", config: {} },
      { kind: "mesen", config: { channelExportMode: 1 } },
    ],
  });
  syncDspFromStore(project, dsp);

  const v = project.systems.view()[0];
  expect(v.platform).toBe("nes");
  const id = v.id;

  audio.renderAudio(1000); // boot + init settle

  // Idle baseline: three pin streams (Pulse / TND / Expansion), each near-silent (n8-midi is silent until MIDI).
  const idle = audio.renderAudioPerChannel(id, 500);
  expect(idle.length).toBe(3); // Pulse, TND, Expansion
  const frames = idle[0].length;
  expect(frames > 0).toBeTruthy();
  idle.forEach((ch) => expect(ch.length).toBe(frames)); // equal-length interleaved-stereo streams
  expect(Math.max(...idle.map(rms)) < 0.01).toBeTruthy();

  // Play a ch1 note → APU Pulse1 → the Pulse pin. Prime the FIFO (n8-midi drops the first MIDI message).
  audio.stageMidiIn(NOTE_ON_CH1);
  audio.stageMidiIn(NOTE_ON_CH1);
  const play = audio.renderAudioPerChannel(id, 1500);
  expect(play.length).toBe(3);
  const [pulse, tnd, expansion] = play.map(rms);
  console.log(`[nes-channels] pulse=${pulse.toFixed(5)} tnd=${tnd.toFixed(5)} exp=${expansion.toFixed(5)}`);

  // Signal flows through the Pulse pin…
  expect(pulse > 0.001).toBeTruthy();
  // …and it's genuinely SEPARATED: the pulse voice lights the Pulse pin while the TND pin (Triangle/Noise/
  // DMC) and the Expansion pin (n8-midi has no expansion chip) stay quiet. A mix copied three ways would
  // make every pin equally loud.
  expect(pulse > tnd * 4).toBeTruthy();
  expect(pulse > expansion * 4).toBeTruthy();

  // The pins are MONO: each interleaved-stereo buffer's right lane is silent (finishBlock writes only the
  // left lane of each stream; the router zeroes the right).
  play.forEach((b) => expect(rightRms(b) < 1e-6).toBeTruthy());

  // Unknown system id → no streams (the null-return path).
  expect(audio.renderAudioPerChannel(999999, 100).length).toBe(0);
});
