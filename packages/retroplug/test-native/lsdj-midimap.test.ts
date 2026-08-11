// Port of legacy test/ts/gb/lsdj/midimap.test.ts: the TS `lsdj-sync` role in MidiMap
// mode (3), running in the real DSP kernel, turns host MIDI NoteOn into LSDj row bytes (ch0 → row,
// ch1 → row+128, NoteOff → 0xFE — dspRoles.ts) and feeds a real LSDj core in SYNC=MI.MAP. A row byte
// triggers that song row live, so mapping a row that has a note makes the core sing.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjProbe, transitions, changeMs, gaps, fmtSample } from "./lsdjPlaybackProbe";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// SYNC=MI.MAP + a one-note song at row 0 so a mapped row-0 byte has something to play.
const MIDIMAP_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
};

const sysStruct = (id: number, mode: string) => ({
  project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
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
    sramBytes: savFrom(MIDIMAP_SONG),
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(sysStruct(id, "midiMap"))).toBeTruthy();

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

// MI.MAP is a SYNC mode, and for a long time this role forgot the sync half: it mapped rows but never
// clocked the cart, so a triggered row sounded its first step and froze there. The test above could not
// see that — a frozen first step is still audible. This one watches POSITION instead of loudness, so a
// cart that triggers but never advances fails.
//
// Numbers are the measured ones from lsdj-playback-probe (B2/B3): 6 clock bytes per phrase step, so
// 96 ticks per 16-step phrase, and one row per single-phrase chain.
const ADVANCE_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap", tempo: 128 },
    rows: [{ chains: [0] }, { chains: [1] }, { chains: [2] }],
    chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }],
    phrases: [0, 1, 2].map((n) => ({
      notes: Array.from({ length: 16 }, () => n + 1),
      instruments: Array.from({ length: 16 }, () => 0),
    })),
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};

test("the midiMap role clocks the cart, so a mapped row actually plays through", () => {
  const p = LsdjProbe.create({ song: ADVANCE_SONG, mode: "midiMap" });
  if (!p) return console.log("# SKIP lsdj-midimap advance: aboy ROM not found / unsupported version");

  // Two stages, so a failure says WHICH half broke. First the launch alone, with the host stopped:
  // that is the pre-existing behaviour and must still trigger the row.
  p.launchNote(0); // ch1 NoteOn note 0 → row 0
  const triggered = p.runFree(500, 50);
  console.log(`[lsdj-midimap] after launch, transport stopped: ${fmtSample(triggered[triggered.length - 1])}`);
  expect(triggered.some((s) => s.playing)).toBeTruthy();

  // Now start the host. The role's clock comes from eachTick, which walkTicks gates on transport, so
  // this is the moment the cart should begin stepping.
  p.bpm(120);
  p.transport(true);
  // At 120 BPM a 24-PPQN tick is 20.8 ms, a 6-tick step is 125 ms and a 96-tick phrase is 2 s, so 8 s
  // covers several rows. Timings are read in MILLISECONDS here: the clock comes from the role's own
  // eachTick, not from bytes this probe wrote, so its tick counter stays at zero.
  const samples = p.runFree(8000, 25);

  const steps = changeMs(samples, (s) => s.channels.pu1.phraseRow);
  const rows = transitions(samples, (s) => s.channels.pu1.songRow);
  console.log(`[lsdj-midimap] pu1 stepped ${steps.length} times over 8 s; rows ${JSON.stringify(rows)}`);
  console.log(`[lsdj-midimap] step spacing (ms): ${JSON.stringify(gaps(steps).slice(0, 10))} (expect ~125 ms at 120 BPM)`);

  // The regression this test exists for: the cart must STEP, and must move past the launched row.
  // Exact trajectory is deliberately not asserted here - it depends on where the clock resyncs when
  // transport starts, which is what the differential test characterises properly.
  expect(steps.length > 20).toBeTruthy();
  expect(rows.length > 1).toBeTruthy();
});
