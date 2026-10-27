// LSDj Master Sync (Arduinoboy "Mode 2 — LSDJ as MIDI Master Sync", lsdj-sync mode 8) — the end-to-end
// functional proof on a real LSDj core. A stock LSDj set to SYNC=LSDj, once playing, self-clocks as the
// serial MASTER and streams one byte out its link port per MIDI-clock tick. Native captures those bytes
// (setSerialOutCapture's `listen`: the internal-clock serialStart path, same serialOutLog_ MI.OUT uses, no
// peer needed, and never `drive`, which would clock a stopped LSDj's wait for another master and turn it
// slave) and feeds them to the DSP kernel, where the mode-8 decoder turns each byte into a 0xF8 clock (+ a
// song-row NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows LSDj's tempo. The
// decoder times the silence on the bytes' own frames, so what it decodes must not depend on how the host cuts
// its blocks, at either rate: the second case holds it to that.
//
// A playing stock LSDj master emits a TEMPO-LOCKED byte stream (~50/sec = ~130 BPM at 24 PPQN: tens a second,
// NOT thousands), and a stopped one goes quiet. The thousands-a-second stream once taken for SYNC=LSDj's raw link
// protocol was the core clocking a stopped LSDj (`drive`, one bit an audio frame: about 5500 bytes a second at
// 44.1 kHz; docs/lsdj-master-sync-fix-design.md section 2). (Master Sync targets stock LSDj, whose SYNC=LSDj is the
// standard link-cable master clock; Arduinoboy builds use MI.OUT / mode 7.)
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { directGraph } from "../src/graph/directGraph";
import { cycle, even } from "./parity/rig";
import { FL_RAGGED } from "./parity/blocks-tiny";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

declare const __RESOURCES_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start
const MASTER_SYNC = "masterSync"; // LsdjSyncMode / lsdj-sync role mode

// SYNC=LSDj + a one-note phrase (the proven cell set from the link-cable tests) so LSDj plays and its
// sequencer advances, driving the master-clock byte stream. No N commands (those are MI.OUT); plain notes.
const MASTER_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};

const SEND_TO_ALL = [{ kind: "midi-routing", config: { mode: "sendToAll" } }];
const lsdjSync = (mode: string) => [{ kind: "lsdj-sync", config: { mode } }];

const messages = (drained: { data: Uint8Array }[]): number[][] => drained.map((m) => Array.from(m.data));

test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte→clock, tempo-locked", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) skip(`lsdj-mastersync: LSDj ROM not found at ${LSDJ}`);

  const audio = createAudioDriver();

  const id = 1;
  expect(graph.add({
    romPath: LSDJ,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: savFrom(MASTER_SONG),
  }, id)).toBeTruthy();

  expect(graph.setProjectStages(SEND_TO_ALL)).toBeTruthy();
  expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
  expect(be.setSerialOutCapture(id, "listen")).toBeTruthy(); // what the store does for mode 8

  audio.renderAudio(6000); // reach the song screen
  // Nothing from the boot: a stopped LSDj master waits on an external clock, which `listen` never supplies.
  expect(messages(audio.drainMidiOut())).toEqual([]);

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
  // The clock stream is LSDj's tempo: 24 PPQN → tens/sec across normal tempos. The core clocking a stopped LSDj
  // (`drive`, one bit an audio frame) would land in the thousands; asserting a tempo band rules it out.
  expect(clocks > 20).toBeTruthy();
  expect(clocksPerSec > 10 && clocksPerSec < 500).toBeTruthy();

  // Stop (START toggles): LSDj plays on until it sees the press, then goes quiet, and 200 ms of silence is one
  // transport stop.
  audio.pressButton(id, START, true);
  audio.renderAudio(80);
  audio.pressButton(id, START, false);
  audio.renderAudio(1200);
  const afterStop = messages(audio.drainMidiOut()).map((m) => m[0]);
  const stop = afterStop.indexOf(0xfc);
  console.log(`[lsdj-mastersync] stop: 0xFC=${afterStop.filter((s) => s === 0xfc).length} clocksBefore=${afterStop.slice(0, Math.max(stop, 0)).filter((s) => s === 0xf8).length}`);
  expect(afterStop.filter((s) => s === 0xfc).length).toBe(1);
  expect(afterStop.slice(stop + 1).length).toBe(0); // nothing while LSDj is stopped
  graph.remove(id);
});

// One run of decoded MIDI as its transport story: "on" (the song-row NoteOn), "start", "stop", and a clock run as its
// length.
function story(sent: number[][]): (string | number)[] {
  const out: (string | number)[] = [];
  for (const m of sent) {
    if (m[0] === 0xf8) {
      if (typeof out[out.length - 1] === "number") (out[out.length - 1] as number)++;
      else out.push(1);
    } else out.push(m[0] === 0xfa ? "start" : m[0] === 0xfc ? "stop" : (m[0] & 0xf0) === 0x90 ? "on" : `?${m[0]}`);
  }
  return out;
}

test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged and 1024-frame blocks, a restart included", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) skip(`lsdj-mastersync: LSDj ROM not found at ${LSDJ}`);
  const audio = createAudioDriver();
  const splits: [string, (n: number) => number[]][] = [
    ["1", (n) => even(n, 1)],
    ["fl", (n) => cycle(n, FL_RAGGED)],
    ["1024", (n) => even(n, 1024)],
  ];
  for (const rate of [44100, 48000]) {
    expect(audio.setSampleRate(rate)).toBeTruthy();
    const frames = (ms: number) => Math.round((rate * ms) / 1000);
    const stories = splits.map(([name, split], i) => {
      const id = 10 + i;
      expect(graph.add({ romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: savFrom(MASTER_SONG) }, id)).toBeTruthy();
      expect(graph.setProjectStages(SEND_TO_ALL)).toBeTruthy();
      expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
      expect(be.setSerialOutCapture(id, "listen")).toBeTruthy();
      const sent: number[][] = [];
      const render = (ms: number) => {
        for (const m of audio.renderBlocks(2, split(frames(ms)).map((n) => ({ frames: n }))).midiOut) sent.push(Array.from(m.data));
      };
      const tap = () => {
        render(10);
        audio.pressButton(id, START, true);
        render(80);
        audio.pressButton(id, START, false);
      };
      render(6000); // power-on to the song screen
      tap(); // play
      render(1500);
      tap(); // stop
      render(1200);
      tap(); // play again
      render(1500);
      // One frame more: a block's bytes reach the decoder with the block after it, so without it a split whose last
      // block is long (a ragged one's can be 4096 frames) would leave that block's clocks undecoded.
      for (const m of audio.renderBlocks(2, [{ frames: 1 }]).midiOut) sent.push(Array.from(m.data));
      graph.remove(id);
      const s = story(sent);
      console.log(`[lsdj-mastersync] ${rate} ${name}: ${JSON.stringify(s)}`);
      return s;
    });
    // Nothing from the boot; a run, its stop, then a fresh run: no clock while stopped, and the restart opens with its
    // own NoteOn and 0xFA.
    const shape = (s: (string | number)[]) => s.map((t) => (typeof t === "number" ? "clocks" : t));
    for (const s of stories) expect(shape(s)).toEqual(["on", "start", "clocks", "stop", "on", "start", "clocks"]);
    // About 51 clocks a second at tempo 128, and within one of each other across the splits: the core's own timing
    // moves by tens of frames with the split, so a byte at a render's edge can land on either side of it.
    for (const at of [2, 6]) {
      const counts = stories.map((s) => s[at] as number);
      for (const n of counts) expect(n > 60 && n < 110).toBeTruthy();
      expect(Math.max(...counts) - Math.min(...counts) <= 1).toBeTruthy();
    }
  }
  expect(audio.setSampleRate(44100)).toBeTruthy(); // back to the engine's default, for a case added after this one
});
