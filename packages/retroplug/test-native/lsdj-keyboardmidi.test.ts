// Final migration task #3: native KeyboardMidi coverage — a real end-to-end functional proof. The TS
// `lsdj-sync` role in KeyboardMidi mode (5), in the real DSP kernel, turns host MIDI notes into LSDj
// PS/2 keyboard scancodes and plays them LIVE on a real LSDj core, audibly.
//
// Getting this to work required two fixes the reference (jkotlinski/keyjazz) + the LSDj manual made
// clear (serial-IN delivery itself needed no new code — the per-byte slave pump in
// SameBoySystem::stepIfBelowTarget, which also serves mGB, already clocks external-clock scancodes in):
//   1. GB-serial byte mangling. KEYBD reads the PS/2 keyboard over the link cable as a serial slave,
//      and the GB serial truncates each PS/2 scancode to 7 bits and reverses them, so the role emits the
//      "as seen by LSDj" values (lsdjKeyboardMap.ts `toGbSerialByte`), matching keyjazz — NOT the
//      textbook PS/2 codes. (This was the actual fix; the bytes were being delivered all along.)
//   2. A running song. Per the manual (§5.6), the keyboard only sounds on the phrase screen or "while
//      the song is running" — LSDj only polls the keyboard (arms SC=0xfc) once playing. So we author a
//      one-note song and press START; the sparse song is mostly silent between its notes, and the
//      keyboard notes fill every window with audio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const START = 7; // GameboyButton::Start

// SYNC=KEYBD + a sparse one-note song: START makes LSDj run (and poll the keyboard), but the song
// itself is silent most of the time, so the keyboard notes are what fill each measured window.
const KEYBD_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Keyboard" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
});

const sysStruct = (id: number, mode: string) => ({
  project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};
const mean = (a: number[]): number => a.reduce((x, y) => x + y, 0) / a.length;

test("KeyboardMidi: MIDI notes play live on a real LSDj via the PS/2-keyboard serial path", () => {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) {
    console.log(`# SKIP lsdj-keyboardmidi: aboy LSDj ROM not found at ${ABOY}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = 1;
  expect(be.constructSystem({
    romPath: ABOY,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: savFromJson(KEYBD_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, "keyboardMidi"))).toBeTruthy();

  audio.renderAudio(6000); // reach the song screen

  // START so LSDj runs and begins polling the keyboard.
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(500); // let playback settle

  // Baseline: the song alone. It's sparse (one note per phrase), so most windows are silent.
  const base: number[] = [];
  for (let i = 0; i < 6; i++) base.push(rms(audio.renderAudio(250)));

  // With keys: a live keyboard note before each window. Every window should now carry audio.
  const withKeys: number[] = [];
  const notes = [48, 52, 55, 60, 64, 67]; // C/E/G across octaves — all map to LSDj piano keys
  for (let i = 0; i < 6; i++) {
    audio.stageMidiIn([0x90, notes[i], 100]);
    withKeys.push(rms(audio.renderAudio(250)));
  }

  console.log(`[lsdj-keyboardmidi] base=[${base.map((x) => x.toFixed(4)).join(",")}] keys=[${withKeys.map((x) => x.toFixed(4)).join(",")}]`);

  // Every with-keys window is audible — the keyboard notes sustain live audio through the render.
  expect(withKeys.every((x) => x > 0.015)).toBeTruthy();
  // The song alone leaves silent gaps (proof the audio is the keyboard, not the song).
  expect(base.filter((x) => x < 0.005).length >= 3).toBeTruthy();
  // And the keyboard clearly raises the average level over the song alone.
  expect(mean(withKeys) > mean(base) + 0.01).toBeTruthy();
});
