// Port of legacy test/ts/gb/lsdj/midimap.test.ts onto greenfield: the TS `lsdj-sync` role in MidiMap
// mode (3), running in the real DSP kernel, turns host MIDI NoteOn into LSDj row bytes (ch0 → row,
// ch1 → row+128, NoteOff → 0xFE — dspRoles.ts) and feeds a real LSDj core in SYNC=MI.MAP. A row byte
// triggers that song row live, so mapping a row that has a note makes the core sing.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// SYNC=MI.MAP + a one-note song at row 0 so a mapped row-0 byte has something to play.
const MIDIMAP_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
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

test("the TS lsdj-sync MidiMap role maps MIDI notes to LSDj row bytes on a real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) {
    console.log(`# SKIP lsdj-midimap: aboy LSDj ROM not found at ${ABOY}`);
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
    sramBytes: savFromJson(MIDIMAP_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, 3))).toBeTruthy();

  audio.renderAudio(6000); // reach the song screen from the sav
  const idle = rms(audio.renderAudio(500)); // no map notes → baseline

  // ch0 NoteOn note 0 → row 0 (which holds our note) → LSDj plays that row live.
  audio.stageMidiIn([0x90, 0, 100]);
  const mapped = rms(audio.renderAudio(2000));
  audio.stageMidiIn([0x80, 0, 0]);
  audio.renderAudio(200);

  const frame = be.getFrame(id);
  console.log(`[lsdj-midimap] idle=${idle.toFixed(5)} mapped=${mapped.toFixed(5)}`);
  expect(frame!.width).toBe(160); // the core survived the row-byte stream (processed MIDI, no crash)
  expect(frame!.height).toBe(144);
  expect(idle < 0.001).toBeTruthy();      // no map note yet → the song row isn't triggered → silent
  expect(mapped > 0.001).toBeTruthy();    // the mapped row-0 byte plays its note → audible
  expect(mapped > idle).toBeTruthy();
});
