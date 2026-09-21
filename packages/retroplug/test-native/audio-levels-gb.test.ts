// What peak level actually comes out of SameBoy, mix and per stem, driven by mGB with every voice at
// velocity 127. Measurement harness, not a regression guard — see audio-levels-lib.ts.
// Run with:  node packages/retroplug/scripts/run-native-tests.mjs audio-levels-gb
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { report, peak, sustain, sustainPerChannel, AUDIBLE } from "./audio-levels-lib";

declare const __DSP_KERNEL_BUNDLE__: string;

// mGB: MIDI ch1..4 → Pulse 1 / Pulse 2 / Wave / Noise. Velocity 127 = max envelope volume.
const NOTES = [[0x90, 48, 127], [0x91, 55, 127], [0x92, 60, 127], [0x93, 40, 127]];
const NAMES = ["Pulse 1", "Pulse 2", "Wave", "Noise"];

test("audio levels: SameBoy (mGB)", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  const id = project.systems.loadMgb()!;
  audio.renderAudio(1500); // boot + firmware init

  NOTES.forEach((n, i) => {
    console.log(`[gb] ${NAMES[i]} alone`);
    report("mix", sustain(audio, [n], 2000, 8));
    sustainPerChannel(audio, id, [n], 2000, 8).forEach((b, k) => report("  " + NAMES[k], b));
    audio.stageMidiIn([0x80 | i, n[1], 0]);
    audio.renderAudio(400);
  });

  console.log("[gb] all 4 voices");
  const mix = report("mix", sustain(audio, NOTES, 3000, 12));
  const all = sustainPerChannel(audio, id, NOTES, 3000, 12);
  const peaks = all.map((b, k) => report(NAMES[k], b));
  console.log(`  sum of stem peaks = ${all.map(peak).reduce((a, b) => a + b, 0).toFixed(4)}`);

  // The measured LEVELS are free - that is what this harness is for - but silence is not a measurement,
  // it is the shape a routing or mixer regression takes, and this file used to report `ok` for it.
  //
  // Per-STEM is deliberately not asserted here. sustain() and sustainPerChannel() are separate renders
  // and each only re-sends NoteOn, so by the second one mGB's pulse envelopes have decayed and those two
  // stems measure 0 even though the mix in the first render was loud. That is this harness's retrigger
  // method, not the 4-stem tap: app-play-mgb-channels.test.ts holds the per-stem claim properly (note-offs
  // between, pulse1 > 0.001, both pulses > 4x noise) and passes. So the claim made here is the one this
  // method supports - the mix sounds, and the split produced a real stream rather than silence.
  expect(mix, "the mix").toBeGreaterThan(AUDIBLE);
  expect(Math.max(...peaks), "the loudest stem").toBeGreaterThan(AUDIBLE);
});
