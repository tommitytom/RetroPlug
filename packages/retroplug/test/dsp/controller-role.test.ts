// The `launchpad` project role running inside the real kernel.
//
// This is the test M5 exists for: a pad press arrives on the block's control-surface stream and comes out
// the other side as bytes on a cart's link port, having passed through the app's quantiser, the target
// seam, a system inbox and the shipped `midiMap` role - in ONE block. Nothing is stubbed but the device
// itself. If the wiring is wrong anywhere along that path, this fails.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type BlockInput, type Sinks } from "../../src/dspKernel";
import { songRowTicks } from "../../src/lsdj/playback";
import { SongSchema } from "../../src/lsdj/model";
import { padIndex } from "../../src/launchpad";

const SYSTEM = 7;

function kernel(): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  return new DspKernel(reg);
}

/** Sixteen rows, each one one-phrase chain: every row has content and lasts 96 ticks. */
const table = () => songRowTicks(SongSchema.parse({
  formatVersion: 22,
  rows: Array.from({ length: 16 }, () => ({ chains: [0, 0, 0, 0] })),
  chains: [{ phrases: [0] }],
}));

/** Press the pad showing (channel 0, row `row`) on page 0. Rows 0-7 are the left pane, 8-15 the right. */
const press = (row: number): number[] => [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100];

const baseDyn = (): BlockInput => ({
  frames: 1024, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: true,
  midiIn: [], buttons: [], keys: [], serialOut: [],
});

function build(k: DspKernel, config: Record<string, unknown> = {}): void {
  k.setSystems({
    project: [
      { kind: "midi-routing", config: { mode: "sendToAll" } },
      { kind: "launchpad", config: { app: "lsdj-midimap", songRowTicks: table(), systemId: SYSTEM, ...config } },
    ],
    // The real MI.MAP translator, not a stand-in: it is what turns the app's NoteOn into a row byte.
    systems: [{ id: SYSTEM, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiMap" } }] }],
  });
}

/** Row bytes only - the clock stream `midiMap` emits every tick would drown the signal. */
const rowBytes = (out: Sinks): number[] =>
  out.serialIn.filter((s) => s.byte !== 0xff && s.byte !== 0xfe).map((s) => s.byte);

test("a pad press reaches the cart's link port, through the real midiMap role, in one block", () => {
  const k = kernel();
  build(k, { appConfig: { quantise: "immediate" } });

  expect(rowBytes(k.processBlock({ ...baseDyn(), ppqStart: 0 }))).toEqual([]); // nothing pressed yet

  const out = k.processBlock({ ...baseDyn(), ppqStart: 0.1, controllerIn: [{ frame: 0, data: press(5) }] });
  expect(rowBytes(out)).toEqual([5]);
  expect(out.serialIn.every((s) => s.system === SYSTEM)).toBeTruthy();
});

test("the same launch stream goes to MIDI out instead, for a real Game Boy", () => {
  // The 7.3 claim: only the sink differs. Same press, same bytes, different destination.
  const k = kernel();
  build(k, { target: "midiOut", appConfig: { quantise: "immediate" } });
  k.processBlock({ ...baseDyn() });

  const out = k.processBlock({ ...baseDyn(), ppqStart: 0.1, controllerIn: [{ frame: 0, data: press(5) }] });
  expect(out.midiOut.map((m) => m.data)).toEqual([[0x90, 5, 100]]);
  expect(rowBytes(out)).toEqual([]); // and nothing went to the cart
});

test("control-surface traffic never reaches the cart as music", () => {
  // The reason controllerIn is a separate stream: a pad press is a NoteOn, and midiMap reads a NoteOn as
  // a row launch. Were it routed with the musical MIDI, every press would fire twice - once quantised by
  // the app and once raw. Here the SAME bytes on midiIn launch row 11 (the pad's note number), while on
  // controllerIn they launch row 5 (what the pad means).
  const k = kernel();
  build(k, { appConfig: { quantise: "immediate" } });
  k.processBlock({ ...baseDyn() });

  const viaMusic = k.processBlock({ ...baseDyn(), ppqStart: 0.1, midiIn: [{ frame: 0, data: press(5) }] });
  expect(rowBytes(viaMusic)).toEqual([padIndex({ x: 0, y: 5 })]); // raw note number, NOT row 5

  const viaSurface = k.processBlock({ ...baseDyn(), ppqStart: 0.2, controllerIn: [{ frame: 0, data: press(5) }] });
  expect(rowBytes(viaSurface)).toEqual([5]);
});

test("quantisation is driven by the block clock, so a launch waits for the bar", () => {
  const k = kernel();
  build(k, { appConfig: { quantise: "bar" } }); // 96 ticks = 4 quarters

  k.processBlock({ ...baseDyn(), ppqStart: 4.0 });
  // Launch something first, so the model is playing and the next press has a boundary to wait for.
  k.processBlock({ ...baseDyn(), ppqStart: 4.1, controllerIn: [{ frame: 0, data: press(0) }] });

  const pressed = k.processBlock({ ...baseDyn(), ppqStart: 4.2, controllerIn: [{ frame: 0, data: press(6) }] });
  expect(rowBytes(pressed)).toEqual([]); // cued, not launched

  expect(rowBytes(k.processBlock({ ...baseDyn(), ppqStart: 7.9 }))).toEqual([]); // still short of the bar
  expect(rowBytes(k.processBlock({ ...baseDyn(), ppqStart: 8.0 }))).toEqual([6]); // ppq 8 = tick 192
});

test("LED bytes come out on the controller sink, not on any system's MIDI", () => {
  const k = kernel();
  build(k);
  const out = k.processBlock({ ...baseDyn() });

  expect(out.controllerOut.length > 0).toBeTruthy(); // the first paint
  expect(out.midiOut.length).toBe(0);
  // A second identical block repaints the same grid, so the diffing surface emits nothing at all.
  expect(k.processBlock({ ...baseDyn() }).controllerOut.length).toBe(0);
});

test("the session survives across blocks, so cues and pages are not forgotten", () => {
  const k = kernel();
  build(k, { appConfig: { quantise: "bar" } });
  k.processBlock({ ...baseDyn(), ppqStart: 0 });
  k.processBlock({ ...baseDyn(), ppqStart: 0.1, controllerIn: [{ frame: 0, data: press(0) }] }); // immediate: nothing playing
  k.processBlock({ ...baseDyn(), ppqStart: 0.2, controllerIn: [{ frame: 0, data: press(9) }] }); // now cued

  // Several blocks later the cue is still pending and still fires - it lives in the project-scope
  // scratch bag, which did not exist before M5.
  expect(rowBytes(k.processBlock({ ...baseDyn(), ppqStart: 3.9 }))).toEqual([]);
  expect(rowBytes(k.processBlock({ ...baseDyn(), ppqStart: 4.0 }))).toEqual([9]);
});

test("an unknown app id runs nothing rather than throwing on the audio thread", () => {
  const k = kernel();
  build(k, { app: "does-not-exist" });
  const out = k.processBlock({ ...baseDyn(), controllerIn: [{ frame: 0, data: press(3) }] });
  expect(out.controllerOut.length).toBe(0);
  expect(rowBytes(out)).toEqual([]);
});

test("systemId 0 means the first system, so a single-cart project needs no id", () => {
  const k = kernel();
  k.setSystems({
    project: [
      { kind: "midi-routing", config: { mode: "sendToAll" } },
      { kind: "launchpad", config: { app: "lsdj-midimap", songRowTicks: table(), appConfig: { quantise: "immediate" } } },
    ],
    systems: [{ id: 42, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiMap" } }] }],
  });
  k.processBlock({ ...baseDyn() });
  const out = k.processBlock({ ...baseDyn(), ppqStart: 0.1, controllerIn: [{ frame: 0, data: press(2) }] });
  expect(out.serialIn.some((s) => s.system === 42 && s.byte === 2)).toBe(true);
});
