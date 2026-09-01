// The NES individual-mono per-channel path (renderAudioPerChannel mode 3), proven end-to-end through the
// real Mesen core + the app stores: n8-midi's notes rendered as the 5 SEPARATE core APU channels —
// Square1, Square2, Triangle, Noise, DMC — instead of the mix. These are raw pre-DAC linear levels that
// explicitly DON'T sum to the mix (§5b); the test proves ISOLATION: a note on one channel lights that
// channel's stream while the other four stay quiet, which a mix copied five ways could never show. The
// streams are MONO (silent right lane). ch1→Square1 is the guaranteed anchor (ch2 is broken in the
// committed ROM, DMC needs a sample bank unavailable headless).
//
// Uses `adopt` (mode 3 arms capture at CONSTRUCT) + a manual syncDspFromStore (adopt is quiet). See
// app-play-nes-channels.test.ts for the same construct-time-capture rationale.
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

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

const rightRms = (a: Float32Array): number => {
  let s = 0, n = 0;
  for (let i = 1; i < a.length; i += 2) { s += a[i] * a[i]; n++; }
  return n ? Math.sqrt(s / n) : 0;
};

const CORE = ["Square1", "Square2", "Triangle", "Noise", "DMC"];

test("NES renders its 5 core channels individually (renderAudioPerChannel mode 3)", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log(`# SKIP app-play-nes-mono: no ROM at ${NES}`); return; }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  project.systems.adopt({
    romPath: NES,
    roles: [
      { kind: "nes-n8-midi", config: {} },
      { kind: "mesen", config: { channelExportMode: "individualMono" } },
    ],
  });
  syncDspFromStore(project, dsp);

  const v = project.systems.view()[0];
  expect(v.platform).toBe("nes");
  const id = v.id;

  audio.renderAudio(1000); // boot + init settle
  // Prime the FIFO once (n8-midi drops the first MIDI message), then release it.
  audio.stageMidiIn([0x90, 60, 100]);
  audio.renderAudio(200);
  audio.stageMidiIn([0x80, 60, 0]);
  audio.renderAudio(300);

  // Idle: five core streams, each near-silent.
  const idle = audio.renderAudioPerChannel(id, 500);
  expect(idle.length).toBe(5); // Square1, Square2, Triangle, Noise, DMC
  const frames = idle[0].length;
  expect(frames > 0).toBeTruthy();
  idle.forEach((ch) => expect(ch.length).toBe(frames));
  expect(Math.max(...idle.map(rms)) < 0.01).toBeTruthy();
  idle.forEach((b) => expect(rightRms(b) < 1e-6).toBeTruthy()); // mono: right lane silent

  // Play a note on one MIDI channel, measure the 5 stems, then release + settle for the next.
  const isolate = (status: number, note: number): number[] => {
    audio.stageMidiIn([status, note, 100]);
    const stems = audio.renderAudioPerChannel(id, 1200).map(rms);
    audio.stageMidiIn([status - 0x10, note, 0]); // note-off (0x80 | ch)
    audio.renderAudio(400);                      // decay before the next channel
    return stems;
  };

  // The guaranteed anchor: ch1 → APU Square1. Its stream rings; the other four stay quiet — proving the
  // 5-way split routes each channel to a distinct stream (a mix copied five ways would light all equally).
  const sq1 = isolate(0x90, 60);
  console.log(`[nes-mono] ch1: ${CORE.map((c, i) => `${c}=${sq1[i].toFixed(4)}`).join(" ")}`);
  expect(sq1[0] > 0.001).toBeTruthy();
  for (let i = 1; i < 5; i++) expect(sq1[0] > sq1[i] * 4).toBeTruthy();

  // ch3 → Triangle (index 2) and ch4 → Noise (index 3, note in the ROM's 36–67 range). Same isolation.
  const tri = isolate(0x92, 67);
  console.log(`[nes-mono] ch3: ${CORE.map((c, i) => `${c}=${tri[i].toFixed(4)}`).join(" ")}`);
  expect(tri[2] > 0.001).toBeTruthy();
  [0, 1, 3, 4].forEach((i) => expect(tri[2] > tri[i] * 4).toBeTruthy());

  const noise = isolate(0x93, 48);
  console.log(`[nes-mono] ch4: ${CORE.map((c, i) => `${c}=${noise[i].toFixed(4)}`).join(" ")}`);
  expect(noise[3] > 0.001).toBeTruthy();
  [0, 1, 2, 4].forEach((i) => expect(noise[3] > noise[i] * 4).toBeTruthy());

  // Unknown system id → no streams.
  expect(audio.renderAudioPerChannel(999999, 100).length).toBe(0);
});
