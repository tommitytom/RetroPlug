// The flagship: the TS `lsdj-sync` role, running IN the real DSP kernel, is the SOLE advancing clock
// for a real LSDj instance. Its MidiSync behaviour is exactly doc-06's shape — eachTick(24) ->
// pushSerialIn(0xF8) — now authored as a plain TS role (dspRoles.ts) rather than an ad-hoc script.
//
// LSDj in SYNC=MIDI is a serial slave: a START press arms it ("wait for MIDI"), then the 0xF8 clock
// advances it. The core constructs bare (no C++ roles) so nothing but the kernel clocks it. The kernel
// drives every system; we toggle THIS system's lsdj-sync mode: Off (0) leaves the armed LSDj
// frozen/SILENT, MidiSync (1) makes it sing. Whole-mix RMS on a single system = that system's audio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// The system's lsdj-sync pipeline at a given LsdjSyncMode (0 = Off, 1 = MidiSync). mode:0 must be
// EXPLICIT — the role's schema defaults a bare config to MidiSync (clocking).
const lsdjSync = (id: number, mode: number) => ({
  systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
});

// SYNC=MIDI + chain0 -> phrase0 -> a C note on a hard-panned pulse (the proven cell set). The codec
// pads every fixed array to full length; syncMode:"Midi" now writes the real LSDj byte 3.
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

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the TS lsdj-sync role in the DSP kernel is the sole clock that makes an armed LSDj sing", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP dsp-lsdj-midisync: LSDj ROM not found at ${LSDJ}`);
    return; // no resources on disk (e.g. resource-less CI) — the devcontainer has it
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Real LSDj, authored SYNC=MIDI song, constructed bare (no C++ roles) so the kernel is the only clock.
  const sav = savFromJson(SYNC_MIDI_SONG);
  const id = be.constructSystem({
    romPath: LSDJ,
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: sav.slice().buffer, // fresh ArrayBuffer at offset 0
  })!;
  expect(id != null).toBeTruthy();

  // Load the real role kernel; per-block drive is the kernel from here on.
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();

  audio.renderAudio(6000); // warm up (transport off) -> LSDj reaches the SONG screen from the sav

  // Arm: a START tap parks SYNC=MIDI LSDj in "wait for MIDI clock".
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(300);

  audio.setBpm(120);
  audio.setTransport(true);

  // Negative: lsdj-sync Off -> the role emits no clock -> armed LSDj stays frozen -> silent.
  expect(dsp.setSystems(lsdjSync(id, 0))).toBeTruthy();
  const neg = rms(audio.renderAudio(600));

  // Positive: lsdj-sync MidiSync -> eachTick(24)->pushSerialIn(0xF8) is the only clock -> LSDj plays.
  expect(dsp.setSystems(lsdjSync(id, 1))).toBeTruthy();
  const pos = rms(audio.renderAudio(3000));

  console.log(`[dsp-lsdj-midisync] neg=${neg.toFixed(5)} pos=${pos.toFixed(5)}`);
  expect(neg < 0.001).toBeTruthy(); // armed but unclocked -> silent
  expect(pos > 0.001).toBeTruthy(); // the kernel's clock advances LSDj -> audible
  expect(pos > neg).toBeTruthy();
});
