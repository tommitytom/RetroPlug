// Native KeyboardMidi coverage (migration task #3), and an honest one. The TS `lsdj-sync` role in
// KeyboardMidi mode (5), in the real DSP kernel, turns host MIDI notes into LSDj PS/2 keyboard
// scancodes (dspRoles.ts) and pushes them to a real LSDj's serial FIFO.
//
// This is a REGRESSION SMOKE: it proves the role runs against a real LSDj core in the render loop
// without throwing. It deliberately does NOT assert that LSDj ACTS on the scancodes, because — proven
// empirically here — it currently doesn't headlessly: keyboard scancodes produce no audio, no SRAM
// change, and no framebuffer change, while a joypad button on the same core DOES change the screen.
// The cause is documented in docs/lsdj.md: LSDj's KEYBD (and MI.OUT) mode reads the GB serial port in
// EXTERNAL-clock mode (SC=0x80), and the headless harness only drives that synthetic clock for
// serial-OUT capture — so the input bytes land in the FIFO but never shift into LSDj. Making this
// functional needs the external-clock INPUT path (entangled with the ArduinoboyMaster / serial-out
// work, migration task #4). Byte-exactness of the MIDI→scancode mapping is covered separately by the
// mock test/dsp/lsdj-modes.test.ts.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// SYNC=KEYBD so LSDj listens to the PS/2 keyboard.
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

test("the TS lsdj-sync KeyboardMidi role runs against a real LSDj core (regression smoke)", () => {
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

  // Drive the role: host MIDI note-ons across octaves → PS/2 scancodes pushed to LSDj's serial FIFO.
  for (const n of [48, 55, 60, 48, 60]) {
    audio.stageMidiIn([0x90, n, 100]);
    audio.renderAudio(180);
  }
  const rmsAfter = rms(audio.renderAudio(200));

  const frame = be.getFrame(id)!;
  // The role fed a real core through the real kernel without throwing, and the core is still alive.
  // (Functional effect on LSDj is not asserted — see the file header: KEYBD needs external-clock serial.)
  console.log(`[lsdj-keyboardmidi] rmsAfter=${rmsAfter.toFixed(5)} (not gated — KEYBD is external-clock serial)`);
  expect(frame.width).toBe(160);
  expect(frame.height).toBe(144);
});
