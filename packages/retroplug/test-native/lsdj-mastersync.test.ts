// LSDj Master Sync (Arduinoboy "Mode 2 — LSDJ as MIDI Master Sync", lsdj-sync mode 8) — the end-to-end
// functional proof on a real LSDj core. A stock LSDj set to SYNC=LSDj, once playing, self-clocks as the
// serial MASTER and streams one byte out its link port per MIDI-clock tick. Native captures those bytes
// (armed via setSerialOutCapture — the internal-clock serialStart path, same serialOutLog_ MI.OUT uses,
// no peer needed) and feeds them to the DSP kernel, where the mode-8 decoder turns each byte into a 0xF8
// clock (+ a song-row NoteOn/0xFA at run start, 0xFC on idle). So the host follows LSDj's tempo.
//
// This resolves the "SYNC=LSDJ is a continuous ~4500 bytes/sec raw link protocol" folklore empirically:
// a stock LSDj-master emits a TEMPO-LOCKED byte stream (~50/sec = ~130 BPM at 24 PPQN, tens/sec — NOT
// thousands). (The Arduinoboy-patched ROM behaves differently in this mode; Master Sync targets stock
// LSDj, whose SYNC=LSDj is the standard link-cable master clock. Arduinoboy builds use MI.OUT / mode 7.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start
const MASTER_SYNC = "masterSync"; // LsdjSyncMode / lsdj-sync role mode

// SYNC=LSDj + a one-note phrase (the proven cell set from the link-cable tests) so LSDj plays and its
// sequencer advances, driving the master-clock byte stream. No N commands (those are MI.OUT); plain notes.
const MASTER_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
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

const messages = (drained: { data: Uint8Array }[]): number[][] => drained.map((m) => Array.from(m.data));

test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte→clock, tempo-locked", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-mastersync: LSDj ROM not found at ${LSDJ}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = 1;
  expect(be.constructSystem({
    romPath: LSDJ,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: savFromJson(MASTER_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, MASTER_SYNC))).toBeTruthy();
  expect(be.setSerialOutCapture(id, true)).toBeTruthy(); // what the store does for mode 8

  audio.renderAudio(6000); // reach the song screen
  audio.drainMidiOut(); // discard boot noise

  // Play: LSDj becomes the internal-clock serial master and streams its sync bytes at tempo.
  audio.pressButton(id, START, true);
  audio.renderAudio(80);
  audio.pressButton(id, START, false);
  const playMs = 4000;
  audio.renderAudio(playMs);

  const drained = messages(audio.drainMidiOut());
  const status = drained.map((m) => m[0]);
  const starts = status.filter((s) => s === 0xfa).length;
  const clocks = status.filter((s) => s === 0xf8).length;
  const noteOns = status.filter((s) => (s & 0xf0) === 0x90).length;
  const clocksPerSec = clocks / ((playMs + 80) / 1000);
  console.log(`[lsdj-mastersync] play: starts=${starts} clocks=${clocks} noteOns=${noteOns} clocks/sec=${clocksPerSec.toFixed(1)}`);

  // Transport start + a song-row NoteOn open the run.
  expect(starts >= 1).toBeTruthy();
  expect(noteOns >= 1).toBeTruthy();
  // The clock stream is LSDj's tempo: 24 PPQN → tens/sec across normal tempos. The old "~4500 bytes/sec"
  // folklore (an Arduinoboy-ROM artifact) would land in the thousands; asserting a tempo band settles it.
  expect(clocks > 20).toBeTruthy();
  expect(clocksPerSec > 10 && clocksPerSec < 500).toBeTruthy();

  // Stop (START toggles): the byte stream goes idle, and the decoder emits a transport stop after the
  // idle threshold.
  audio.pressButton(id, START, true);
  audio.renderAudio(80);
  audio.pressButton(id, START, false);
  audio.renderAudio(1200);
  const afterStop = messages(audio.drainMidiOut()).map((m) => m[0]);
  console.log(`[lsdj-mastersync] stop: 0xFC=${afterStop.filter((s) => s === 0xfc).length} suppressedClocks=${afterStop.filter((s) => s === 0xf8).length}`);
  expect(afterStop.includes(0xfc)).toBeTruthy(); // stopped LSDj floods the idle handshake → one 0xFC
  expect(afterStop.filter((s) => s === 0xf8).length).toBe(0); // and the flood emits no spurious clocks
});
