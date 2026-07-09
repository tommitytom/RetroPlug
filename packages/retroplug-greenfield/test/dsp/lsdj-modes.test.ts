// The LSDj sync modes ported off LsdjSyncRole.cpp, driven through the DSP kernel end-to-end (pure TS,
// no backend). Each case authors input MIDI / transport and asserts the serial (or sentinel) bytes the
// mode pushes into LSDj — the doc-06 "translators are scripts" shape as plain TS. Also exercises the
// per-system scratch state the kernel now persists across blocks (Arduinoboy play flag / divisor,
// MidiMap last row, keyboard octave, transport edge) and its pruning on system removal.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type BlockInput } from "../../src/dspKernel";
import type { MidiEvent } from "../../src/midiRouting";

// A one-system project with the given lsdj-sync mode, plus SendToAll routing so system 1 receives every
// host-MIDI event verbatim (channel nibble intact — MidiMap keys on it).
function lsdj(mode: number, config: Record<string, unknown> = {}): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  const k = new DspKernel(reg);
  k.setSystems({
    project: [{ kind: "midi-routing", config: { mode: 0 } }],
    systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode, ...config } }] }],
  });
  return k;
}

// 22050 frames @ 44100 / 120 = exactly 1 beat (24 ticks at 24 PPQN).
const baseDyn = (): BlockInput => ({
  frames: 22050, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false, midiIn: [], buttons: [], keys: [],
});
const noteOn = (channel: number, note: number, frame = 0): MidiEvent => ({ frame, data: [0x90 | channel, note, 100] });
const noteOff = (channel: number, note: number, frame = 0): MidiEvent => ({ frame, data: [0x80 | channel, note, 0] });
const bytes = (out: { serialIn: { byte: number }[] }): number[] => out.serialIn.map((s) => s.byte);
const clocks = (out: { serialIn: { byte: number }[] }): number => out.serialIn.filter((s) => s.byte === 0xf8).length;

test("MidiPassthrough forwards raw MIDI bytes verbatim to serial", () => {
  const out = lsdj(6).processBlock({ ...baseDyn(), midiIn: [noteOn(0, 60)] });
  expect(bytes(out)).toEqual([0x90, 60, 100]);
});

test("MidiMap: ch0 NoteOn → row byte, matching NoteOff → 0xFE, ch1 → row+128 (lastRow persists across blocks)", () => {
  const k = lsdj(3);
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOn(0, 5)] }))).toEqual([5]); // ch0 note 5 → row 5
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOff(0, 5)] }))).toEqual([0xfe]); // matching off → sentinel
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOff(0, 9)] }))).toEqual([]); // non-matching off → nothing
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOn(1, 5)] }))).toEqual([133]); // ch1 note 5 → 5 + 128
});

test("MidiSyncArduinoboy: note 24 arms the clock behind a 0xFA bookend; note 25 stops it with 0xFC", () => {
  const k = lsdj(2);
  expect(k.processBlock({ ...baseDyn() }).serialIn.length).toBe(0); // idle: not playing, transport off

  // Note 24 arms play; transport rises this block → 0xFA first, then a full 24-tick clock (divisor 1).
  const started = k.processBlock({ ...baseDyn(), transport: true, midiIn: [noteOn(0, 24)] });
  expect(started.serialIn[0].byte).toBe(0xfa);
  expect(clocks(started)).toBe(24);

  // Still playing, transport already true → clock continues drift-free, no new bookend.
  const cont = k.processBlock({ ...baseDyn(), ppqStart: 1, transport: true });
  expect(cont.serialIn.some((s) => s.byte === 0xfa)).toBeFalsy();
  expect(clocks(cont)).toBe(24);

  // Note 25 stops play; transport falls → a lone 0xFC bookend, no clock.
  expect(bytes(k.processBlock({ ...baseDyn(), ppqStart: 2, transport: false, midiIn: [noteOn(0, 25)] }))).toEqual([0xfc]);
});

test("MidiSyncArduinoboy: tempo divisor subdivides the clock; note >= 30 pushes a raw row byte", () => {
  // 24 / 2 = 12 ticks/beat (fresh kernel — no mid-stream resolution change).
  expect(clocks(lsdj(2, { tempoDivisor: 2 }).processBlock({ ...baseDyn(), transport: true, midiIn: [noteOn(0, 24)] }))).toBe(12);
  // note 40 → 40 - 30 = 10 (row passthrough is independent of the play flag).
  expect(bytes(lsdj(2).processBlock({ ...baseDyn(), midiIn: [noteOn(0, 40)] }))).toEqual([10]);
});

test("KeyboardMidi: a note maps to its PS/2 scancode (sliding the octave); a cursor note gets the 0xE0 prefix", () => {
  const k = lsdj(5);
  // note 48 (C-3 = NOTE_START): octave slides 4 → 0 (4× OCT_DN 0x05) then NOTE_MAP[0] = 0x1A.
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOn(0, 48)] }))).toEqual([0x05, 0x05, 0x05, 0x05, 0x1a]);
  // note 40 (LOW_START 36 + 4 = Cursor Left, 0x68) → extended 0xE0 prefix then the code.
  expect(bytes(k.processBlock({ ...baseDyn(), midiIn: [noteOn(0, 40)] }))).toEqual([0xe0, 0x68]);
});

test("the kernel prunes per-system scratch state: a removed-then-readded system starts fresh", () => {
  const k = lsdj(2);
  k.processBlock({ ...baseDyn(), transport: true, midiIn: [noteOn(0, 24)] }); // arm play + store prevTransport=true

  // Remove system 1 (prunes its state bag), then re-add it.
  k.setSystems({ project: [{ kind: "midi-routing", config: { mode: 0 } }], systems: [] });
  k.setSystems({
    project: [{ kind: "midi-routing", config: { mode: 0 } }],
    systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: 2 } }] }],
  });

  // Fresh state: play flag reset (no clock), and transport true reads as a fresh rising edge (0xFA).
  const out = k.processBlock({ ...baseDyn(), transport: true });
  expect(clocks(out)).toBe(0);
  expect(out.serialIn.some((s) => s.byte === 0xfa)).toBeTruthy();
});
