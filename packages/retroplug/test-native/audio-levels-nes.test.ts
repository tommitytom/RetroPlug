// What peak level actually comes out of Mesen's NES core: the mix, plus the five raw pre-DAC core
// channels (channelExportMode individualMono), driven by n8-midi with every 2A03 voice at velocity 127.
// Measurement harness, not a regression guard — see audio-levels-lib.ts.
// Run with:  node packages/retroplug/scripts/run-native-tests.mjs audio-levels-nes
import { test } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { report, sustain, sustainPerChannel } from "./audio-levels-lib";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
// n8-midi: ch1→Pulse1, ch2→Pulse2, ch3→Triangle, ch4→Noise, ch5→DMC.
const NOTES = [[0x90, 48, 127], [0x91, 55, 127], [0x92, 43, 127], [0x93, 60, 127], [0x94, 60, 127]];
const NAMES = ["Pulse 1", "Pulse 2", "Triangle", "Noise", "DMC"];

test("audio levels: Mesen NES mix + core channels (n8-midi)", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log(`# SKIP: no ROM at ${NES}`); return; }
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  project.systems.adopt({
    romPath: NES,
    roles: [
      { kind: "nes-n8-midi", config: {} },
      { kind: "mesen", config: { channelExportMode: "individualMono" } },
    ],
  });
  syncDspFromStore(project, dsp);
  const id = project.systems.view()[0].id;

  audio.renderAudio(1500);
  audio.stageMidiIn(NOTES[0]); // n8-midi drops its first MIDI message
  audio.renderAudio(200);

  NOTES.forEach((n, i) => {
    console.log(`[nes] ${NAMES[i]} alone`);
    report("mix", sustain(audio, [n], 2000, 8));
    sustainPerChannel(audio, id, [n], 2000, 8).forEach((b, k) => report("  " + NAMES[k], b));
    audio.stageMidiIn([0x80 | i, n[1], 0]);
    audio.renderAudio(400);
  });

  console.log("[nes] all 5 voices");
  report("mix", sustain(audio, NOTES, 3000, 12));
  sustainPerChannel(audio, id, NOTES, 3000, 12).forEach((b, k) => report(NAMES[k], b));
});
