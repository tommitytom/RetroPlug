// C1: host MIDI drives a NES core. The whole path end-to-end against a REAL Mesen core — routing →
// the nes-n8-midi role → the emitCoreMidi sink → Engine fans it to onMidi → the always-attached N8
// FIFO → bliptoaster.nes plays APU Pulse1. Proven by RMS: silent when idle, audible after a ch1 NoteOn.
// Also closes the F3 gap (deferred earlier because NES had no audio): the universal gain setting now
// audibly silences/restores a ringing NES core.
//
// NOTE: channel 2 of bliptoaster.nes is broken (ROM fixed later) — drive channel 1 only (ch1 → Pulse1).
// A fresh NoteOn is staged before each measurement so the proof doesn't depend on the ROM sustaining.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const NOTE_ON_CH1 = [0x90, 60, 100]; // ch1 NoteOn C4 → APU Pulse1

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("host MIDI drives a NES core (ch1 → Pulse1), and live gain silences/restores it", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log(`# SKIP app-play-nes: no ROM at ${NES}`); return; }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Ownership discipline (per dsp-threaded): kernel loaded + store→DSP hook installed before audio.
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = (project.systems.loadRom(NES) as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe("nes");
  // The NES rom provider attached the host-MIDI role → the emitCoreMidi path is in the pipeline.
  expect(project.systems.view()[0].roles.map((r) => r.kind).includes("nes-n8-midi")).toBeTruthy();

  audio.renderAudio(1000); // boot; NES has no boot screen but let the ROM's init settle
  const idle = rms(audio.renderAudio(500));

  audio.stageMidiIn(NOTE_ON_CH1);
  const playing = rms(audio.renderAudio(1500));

  console.log(`[app-play-nes] idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.001).toBeTruthy();     // silent until MIDI-driven (memory: SILENT until MIDI)
  expect(playing > 0.001).toBeTruthy();  // the ch1 note reached the core → Pulse1 rings
  expect(playing > idle).toBeTruthy();   // host MIDI genuinely drives the NES audio

  // --- F3 closure: the universal gain setting reaches the live NES core. Re-stage a note at each
  // level so RMS measures the gain, not any note decay; discard a short settle window after each edit. ---
  expect(project.systems.setGain(id, -90)).toBeTruthy();
  audio.renderAudio(150);                // let the gain smoother ramp down
  audio.stageMidiIn(NOTE_ON_CH1);
  const silenced = rms(audio.renderAudio(500));

  expect(project.systems.setGain(id, 0)).toBeTruthy();
  audio.renderAudio(150);                // ramp back up
  audio.stageMidiIn(NOTE_ON_CH1);
  const restored = rms(audio.renderAudio(1500));

  console.log(`[app-play-nes] silenced=${silenced.toFixed(5)} restored=${restored.toFixed(5)}`);
  expect(silenced < 0.001).toBeTruthy();   // -90 dB floored the ringing voice → gain reaches the NES core
  expect(restored > silenced).toBeTruthy(); // 0 dB brought it back
});
