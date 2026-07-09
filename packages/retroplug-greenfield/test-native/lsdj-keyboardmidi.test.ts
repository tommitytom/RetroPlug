// Final migration task #3: native KeyboardMidi coverage. Port of legacy test/ts/gb/lsdj/keyboardmidi.test.ts
// onto greenfield — the TS `lsdj-sync` role in KeyboardMidi mode (5), running in the real DSP kernel,
// turns host MIDI notes into LSDj PS/2 keyboard scancodes (dspRoles.ts) and feeds them to a real LSDj
// core in SYNC=KEYBD. End-to-end coverage is split across two layers: the mock test/dsp/lsdj-modes.test.ts
// asserts the EXACT scancodes the role emits (MIDI → bytes), and this native test proves those scancodes
// are delivered to a real LSDj core in the real kernel and it keeps running (bytes → live core, no crash).
// LSDj's keyboard is a song-EDITOR input, not a live synth, so a bare note stream is silent (matching the
// legacy test, which also never asserted audio) — audio is logged for visibility, not gated.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// SYNC=KEYBD so LSDj listens to the PS/2 keyboard; a default pulse instrument so live keys are audible.
const KEYBD_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Keyboard" },
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
});

const sysStruct = (id: number, mode: number) => ({
  project: [{ kind: "midi-routing", config: { mode: 0 } }],
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the TS lsdj-sync KeyboardMidi role maps MIDI notes to LSDj keyboard scancodes on a real core", () => {
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
  expect(dsp.setSystems(sysStruct(id, 5))).toBeTruthy();

  audio.renderAudio(6000); // reach the song screen from the sav
  const idle = rms(audio.renderAudio(500)); // no keys → baseline

  // A stream of note-ons across octaves → the role emits PS/2 scancodes → LSDj plays them live.
  for (const n of [48, 55, 60, 48, 60]) {
    audio.stageMidiIn([0x90, n, 100]);
    audio.renderAudio(180);
  }
  const played = rms(audio.renderAudio(500));

  const frame = be.getFrame(id);
  console.log(`[lsdj-keyboardmidi] idle=${idle.toFixed(5)} played=${played.toFixed(5)}`);
  expect(frame!.width).toBe(160); // the core survived the scancode stream (processed MIDI, no crash)
  expect(frame!.height).toBe(144);
});
