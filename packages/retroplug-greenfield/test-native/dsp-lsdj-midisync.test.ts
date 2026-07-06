// The flagship: a DSP script's 24-PPQN clock (eachTick(24) -> pushSerialIn(0xF8)) is the SOLE
// advancing clock for a real LSDj instance. doc-06 names this exact shape ("LsdjSyncRole's MidiSync
// clock is just eachTick(...) -> pushSerialIn(0xF8)").
//
// LSDj in SYNC=MIDI is a serial slave: a START press arms it ("wait for MIDI"), then the 0xF8 clock
// advances it — so the DSP clock is pure 0xF8. The C++ sync role is forced Off so nothing but the
// DSP clocks it: the armed LSDj is SILENT with the DSP detached and AUDIBLE once it's attached.
// Whole-mix RMS on a single system = that system's audio. In-TS RMS (no reaper).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// The literal doc-06 flagship: a 24-PPQN MIDI clock, nothing else.
const CLOCK = `function onBlock(input){ eachTick(24, function(t, o){ pushSerialIn(o, 0xF8); }); }`;

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

test("a DSP eachTick->pushSerialIn(0xF8) clock is the sole clock that makes an armed LSDj sing", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP dsp-lsdj-midisync: LSDj ROM not found at ${LSDJ}`);
    return; // no resources on disk (e.g. resource-less CI) — the devcontainer has it
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Real LSDj, authored SYNC=MIDI song, its C++ sync role forced Off so the DSP is the only clock.
  const sav = savFromJson(SYNC_MIDI_SONG);
  const id = be.constructSystem({
    romPath: LSDJ,
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: sav.slice().buffer, // fresh ArrayBuffer at offset 0
    lsdjSyncMode: "Off",
  })!;
  expect(id != null).toBeTruthy();

  audio.renderAudio(6000); // warm up (transport off) -> LSDj reaches the SONG screen from the sav

  // Arm: a START tap parks SYNC=MIDI LSDj in "wait for MIDI clock".
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(300);

  audio.setBpm(120);
  audio.setTransport(true);

  // Negative: DSP DETACHED -> role is Off, nothing clocks -> armed LSDj stays frozen -> silent.
  const neg = rms(audio.renderAudio(600));

  // Positive: attach the DSP clock -> eachTick(24)->pushSerialIn(0xF8) is the only clock -> LSDj plays.
  expect(dsp.loadScript(dsp.compileScript(CLOCK)!)).toBeTruthy();
  expect(audio.dspAttach(id)).toBeTruthy();
  const pos = rms(audio.renderAudio(3000));

  console.log(`[dsp-lsdj-midisync] neg=${neg.toFixed(5)} pos=${pos.toFixed(5)}`);
  expect(neg < 0.001).toBeTruthy(); // armed but unclocked -> silent
  expect(pos > 0.001).toBeTruthy(); // the DSP clock advances LSDj -> audible
  expect(pos > neg).toBeTruthy();
});
