// Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) — the full end-to-end functional proof on a
// real LSDj core. A song authored with SYNC=MI.OUT + N (NoteOn) commands, once playing, emits the
// Arduinoboy protocol on LSDj's serial-out port; native captures those raw bytes (armed via
// setSerialOutCapture) and feeds them to the DSP kernel, where the mode-7 decoder strips the flag-gated
// framing + decodes the byte protocol into host MIDI (emitMidiOut). We drain that MIDI and assert a
// transport START (0xFA), a clock stream (0xF8), and NoteOns (0x9x).
//
// This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT
// is byte 9 (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing — which the raw 8-bit
// capture had mis-read as garbage — decodes cleanly.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const START = 7; // GameboyButton::Start
const MIDIOUT = 7; // LsdjSyncMode / lsdj-sync role mode

// A phrase of eight rows, each an audible note plus an `N` (NoteOn) command whose value is the MIDI note
// to transmit — that command is what drives the MI.OUT NoteOn protocol. SYNC=MI.OUT so LSDj emits the
// Arduinoboy stream (clock + START on play, NoteOns from the N commands).
const MIDIOUT_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiOut" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [
      {
        notes: [1, 13, 25, 1, 13, 25, 1, 13],
        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
      },
    ],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
});

const sysStruct = (id: number, mode: number) => ({
  project: [{ kind: "midi-routing", config: { mode: 0 } }],
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

// Flatten a drainMidiOut result into the raw status/data byte messages for assertions.
const messages = (drained: { data: Uint8Array }[]): number[][] => drained.map((m) => Array.from(m.data));

test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the kernel decodes to host MIDI", () => {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) {
    console.log(`# SKIP lsdj-midiout: aboy LSDj ROM not found at ${ABOY}`);
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
    sramBytes: savFromJson(MIDIOUT_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, MIDIOUT))).toBeTruthy();

  // Arm serial-out capture (what the store does when a system's lsdj-sync mode is MIDIOUT).
  expect(be.setSerialOutCapture(id, true)).toBeTruthy();

  audio.renderAudio(6000); // reach the song screen
  audio.drainMidiOut(); // discard any boot-time noise

  // LSDj's START button toggles play/stop; each tap sends the transport bookend on its serial-out (the
  // MI.OUT stream — LSDj is the serial master and self-clocks). We play, stop, then play again: the
  // stop→restart lands a clean 0xFC/0xFA transport edge mid-stream, after the external-clock serial link
  // has settled (the very first byte at play-start can be lost to the capture-arming handshake).
  const tap = () => {
    audio.pressButton(id, START, true);
    audio.renderAudio(80);
    audio.pressButton(id, START, false);
  };
  tap(); // play
  audio.renderAudio(2000); // let the phrase run so N commands + clocks transmit
  tap(); // stop → 0xFC
  audio.renderAudio(400);
  tap(); // play again → 0xFA, mid-stream where framing is aligned
  audio.renderAudio(2000);

  const drained = messages(audio.drainMidiOut());
  const status = drained.map((m) => m[0]);
  const starts = status.filter((s) => s === 0xfa).length;
  const stops = status.filter((s) => s === 0xfc).length;
  const clocks = status.filter((s) => s === 0xf8).length;
  const noteOns = status.filter((s) => (s & 0xf0) === 0x90).length;
  console.log(`[lsdj-midiout] ${drained.length} msgs; starts=${starts} stops=${stops} clocks=${clocks} noteOns=${noteOns}`);

  // A clock stream — LSDj clocks its MI.OUT output while playing (the transport heartbeat).
  expect(clocks > 4).toBeTruthy();
  // NoteOns from the N commands — the actual musical MIDI content.
  expect(noteOns > 0).toBeTruthy();
  // A transport bookend — the stop→restart toggle sends 0xFC then 0xFA on the (aligned) MI.OUT stream.
  expect(starts + stops > 0).toBeTruthy();
});
