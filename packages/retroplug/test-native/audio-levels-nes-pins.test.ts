// The other NES per-channel shape: the two 2A03 "stereo-mod" pins (Pulse | TND) plus the lumped
// Expansion term (channelExportMode stereoModPins). Measurement harness, not a regression guard.
// Run with:  node packages/retroplug/scripts/run-native-tests.mjs audio-levels-nes-pins
import { test } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { report, sustainPerChannel } from "./audio-levels-lib";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const NOTES = [[0x90, 48, 127], [0x91, 55, 127], [0x92, 43, 127], [0x93, 60, 127], [0x94, 60, 127]];
const PINS = ["Pulse pin", "TND pin", "Expansion"];

test("audio levels: Mesen NES 2A03 pins (n8-midi, stereoModPins)", () => {
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
      { kind: "mesen", config: { channelExportMode: "stereoModPins" } },
    ],
  });
  syncDspFromStore(project, dsp);
  const id = project.systems.view()[0].id;

  audio.renderAudio(1500);
  audio.stageMidiIn(NOTES[0]);
  audio.renderAudio(200);

  console.log("[nes pins] both pulses");
  sustainPerChannel(audio, id, NOTES.slice(0, 2), 2000, 8).forEach((b, k) => report(PINS[k], b));
  console.log("[nes pins] triangle + noise + DMC");
  sustainPerChannel(audio, id, NOTES.slice(2), 2000, 8).forEach((b, k) => report(PINS[k], b));
  console.log("[nes pins] all 5 voices");
  sustainPerChannel(audio, id, NOTES, 3000, 12).forEach((b, k) => report(PINS[k], b));
});
