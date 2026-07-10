// Port of legacy test/ts/gb/lsdj/arduinoboy_input.test.ts onto greenfield: the TS `lsdj-sync` role in
// MidiSyncArduinoboy mode (2), running in the real DSP kernel, drives a real LSDj (aboy build) as a
// slave. The contract (dspRoles.ts / LsdjSyncRole.cpp): a `play` note (24) arms the clock, `stop`
// (25) disarms it; the 0xF8 clock only flows while playing AND transport runs. So LSDj sings only
// after the play command — the gating the legacy screenshots only implied, asserted here via RMS.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

// The aboy build is required for the Arduinoboy sync families (docs/lsdj.md ROM table).
const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// SYNC=LSDj (Arduinoboy slave) + a one-note song on a hard-panned pulse (the proven cell set).
const SLAVE_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
});

// A one-system project with the given lsdj-sync mode + SendToAll routing so system 1 receives every
// host-MIDI event verbatim (the Arduinoboy control notes ride channel-agnostic).
const sysStruct = (id: number, mode: number) => ({
  project: [{ kind: "midi-routing", config: { mode: 0 } }],
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the TS lsdj-sync Arduinoboy-slave role plays a real LSDj on note-24, gates it before", () => {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) {
    console.log(`# SKIP lsdj-arduinoboy-slave: aboy LSDj ROM not found at ${ABOY}`);
    return; // no resources on disk (e.g. resource-less CI) — the devcontainer has it
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = 1; // direct-backend test picks its own id (fresh host per file)
  expect(be.constructSystem({
    romPath: ABOY,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: savFromJson(SLAVE_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, 2))).toBeTruthy();

  audio.renderAudio(6000); // valid sav skips the self-test; still needs a few s to the song screen

  // Transport on but no "play" note yet: the role emits the 0xFA start bookend but gates the clock —
  // LSDj is armed and parked.
  audio.setBpm(120);
  audio.setTransport(true);
  const beforePlay = rms(audio.renderAudio(1500));

  // note 24 = Arduinoboy "play": the role's clock now flows → LSDj advances → audio.
  audio.stageMidiIn([0x90, 24, 100]);
  const playing = rms(audio.renderAudio(4000));

  // note 25 = "stop": clock gated off again → playback winds down.
  audio.stageMidiIn([0x90, 25, 100]);
  const afterStop = rms(audio.renderAudio(3000));

  console.log(`[lsdj-arduinoboy-slave] beforePlay=${beforePlay.toFixed(5)} playing=${playing.toFixed(5)} afterStop=${afterStop.toFixed(5)}`);
  expect(playing > 0.001).toBeTruthy();      // play command + clock → audio
  expect(playing > beforePlay).toBeTruthy(); // gated before the play command
});
