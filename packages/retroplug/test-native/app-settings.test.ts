// Phase 1: system settings actually take effect on the LIVE core, driven through the stores. Gain is
// the crisp proof — a -90 dB edit silences a ringing mGB and 0 dB restores it, so applySystemSetting
// reaches the running emulator (not the old no-op stub). Then the "sameboy" system-role config
// (highpass live, model via restart) dispatches through applyRoleConfig without breaking playback.
// One test / one mGB system: whole-mix RMS needs a single system in the shared native Project.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

// A C-major chord (one note per mGB pulse channel) that mGB sustains — so a later gain edit acts on
// a still-ringing voice.
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

test("live gain silences/restores a ringing mGB, and sameboy role config dispatches (through the stores)", () => {
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

  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const loud = rms(audio.renderAudio(500)); // the chord rings

  // --- live gain: the crisp proof that applySystemSetting reaches the running core. Discard a short
  // settle window after each edit so RMS measures the settled level, not the ~20 ms gain ramp. ---
  expect(project.systems.setGain(id, -90)).toBeTruthy(); // floor → ~silence
  audio.renderAudio(150); // let the gain smoother ramp down (discard the transient)
  const silenced = rms(audio.renderAudio(500));
  expect(project.systems.setGain(id, 0)).toBeTruthy(); // unity → audible again
  audio.renderAudio(150); // ramp back up
  const restored = rms(audio.renderAudio(500));

  console.log(`[app-settings] loud=${loud.toFixed(5)} silenced=${silenced.toFixed(5)} restored=${restored.toFixed(5)}`);
  expect(loud > 0.001).toBeTruthy(); // the chord is audible
  expect(silenced < 0.001).toBeTruthy(); // -90 dB attenuated it to silence
  expect(restored > silenced).toBeTruthy(); // 0 dB brought it back — gain edits reach the live core

  // --- sameboy system-role config: highpass is live, model triggers a restart. Both must dispatch
  // through applyRoleConfig and leave the system playing. ---
  expect(project.systems.setRoleConfig(id, "sameboy", { highpass: "removeDcOffset" })).toBeTruthy(); // RemoveDcOffset, live
  const afterHighpass = rms(audio.renderAudio(500));
  expect(afterHighpass > 0.001).toBeTruthy(); // still ringing after a live filter change

  expect(project.systems.setRoleConfig(id, "sameboy", { model: "dmgB" })).toBeTruthy(); // DmgB → restartEmulator
  audio.renderAudio(1500); // the core rebooted — warm it up again
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const afterModel = rms(audio.renderAudio(1500));
  console.log(`[app-settings] afterHighpass=${afterHighpass.toFixed(5)} afterModel=${afterModel.toFixed(5)}`);
  expect(afterModel > 0.001).toBeTruthy(); // survived the model-change restart and plays on the new model
});
