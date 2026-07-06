// The flagship through the THREADED path: the TS lsdj-sync role runs on a real background AUDIO
// THREAD while the control thread flips its mode over the lock-free command queue. This is the
// audio-thread / control-thread seam a DAW imposes, made testable headlessly — and under TSan
// (tools/run-greenfield-tsan.sh) it proves the QuickJS DSP context is touched only by the audio
// thread (structure edits cross as queued commands, transport as atomics). Coarse silent→audible
// over a real time window (a concurrency test, not exact samples). In-TS RMS.
//
// Ownership discipline: cores are quiescent while the audio thread runs — the system is constructed
// and the kernel loaded BEFORE startAudio; nothing reads core state until after stopAudio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// The system's lsdj-sync pipeline at a given LsdjSyncMode (0 = Off, 1 = MidiSync).
const lsdjSync = (id: number, mode: number) => ({
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

// SYNC=MIDI + a C note on a hard-panned pulse (the proven flagship cell set).
const SYNC_MIDI_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Midi" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
});

test("the lsdj-sync role clocks LSDj on a background audio thread, toggled via the command queue", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP dsp-threaded: LSDj ROM not found at ${LSDJ}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // --- setup (single-threaded, before the audio thread) ---
  const sav = savFromJson(SYNC_MIDI_SONG);
  const id = be.constructSystem({
    romPath: LSDJ,
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: sav.slice().buffer,
    // Cores construct bare (no C++ roles) — the TS lsdj-sync kernel role is the sole clock.
  })!;
  expect(id != null).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(lsdjSync(id, 0))).toBeTruthy(); // structure known, clock off

  audio.renderAudio(6000); // reach the SONG screen from the sav (pull path)
  audio.pressButton(id, START, true); // arm SYNC=MIDI LSDj ("wait for MIDI clock")
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(300);
  audio.setBpm(120);
  audio.setTransport(true);

  // --- threaded: the audio thread free-runs; control edits cross via the command queue ---
  expect(audio.startAudio()).toBeTruthy();

  // Windowed RMS over the free-running audio thread: diff two monotonic capture snapshots.
  const rmsWindow = (ms: number): number => {
    const a = audio.audioCaptured();
    audio.sleepMs(ms);
    const b = audio.audioCaptured();
    const df = b.frames - a.frames;
    return df > 0 ? Math.sqrt((b.energy - a.energy) / df) : 0;
  };

  const neg = rmsWindow(150); // lsdj-sync Off → no clock → armed LSDj frozen → silent
  expect(dsp.setSystems(lsdjSync(id, 1))).toBeTruthy(); // enqueue mode:1; applied on the audio thread
  const pos = rmsWindow(400); // the kernel's clock now advances LSDj → audible

  expect(audio.stopAudio()).toBeTruthy();

  console.log(`[dsp-threaded] neg=${neg.toFixed(5)} pos=${pos.toFixed(5)}`);
  expect(neg < 0.001).toBeTruthy(); // armed but unclocked → silent
  expect(pos > 0.001).toBeTruthy(); // clocked from the audio thread → audible
  expect(pos > neg).toBeTruthy();
});
