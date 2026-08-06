// What peak level actually comes out of SameBoy, mix and per stem, driven by mGB with every voice at
// velocity 127. Measurement harness, not a regression guard — see audio-levels-lib.ts.
// Run with:  node packages/retroplug/scripts/run-native-tests.mjs audio-levels-gb
import { test } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { report, peak, sustain, sustainPerChannel } from "./audio-levels-lib";

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
  report("mix", sustain(audio, NOTES, 3000, 12));
  const all = sustainPerChannel(audio, id, NOTES, 3000, 12);
  all.forEach((b, k) => report(NAMES[k], b));
  console.log(`  sum of stem peaks = ${all.map(peak).reduce((a, b) => a + b, 0).toFixed(4)}`);
});
