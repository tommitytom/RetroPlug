// Unit tests for the Arduinoboy MIDIOUT (MI.OUT) decoder (src/lsdjArduinoboy.ts) — the TS port of the
// native ArduinoboyMaster role. Two layers:
//   - arduinoboyDecodeByte: the byte protocol. These 11 cases are a direct port of the native
//     packages/native/test/ArduinoboyMasterTests.cpp (same bytes in, same MIDI out).
//   - arduinoboyDecodeSerialOut: the flag-gated framing in front of it — the raw captured bytes LSDj
//     actually emits (each frame = 1 flag bit + 7 payload bits, MSB-first), decoded to the same MIDI.
import { test, expect } from "../../testing/harness";
import {
  arduinoboyDecodeByte,
  arduinoboyDecodeSerialOut,
  arduinoboyMasterSyncBlock,
  arduinoboyReset,
  type ArduinoboyState,
  type MasterSyncState,
} from "../../src/lsdjArduinoboy";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type BlockInput } from "../../src/dspKernel";

// Feed a run of protocol bytes through a fresh decoder, returning the emitted MIDI messages.
function decode(bytes: number[]): number[][] {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  for (const b of bytes) arduinoboyDecodeByte(b, state, (d) => out.push(d));
  return out;
}

// --- arduinoboyDecodeByte: the 11 ArduinoboyMasterTests.cpp cases -------------------------------

test("ArduinoboyMaster: 0x7F → MIDI clock tick (0xF8)", () => {
  const evs = decode([0x7f, 0x7f, 0x7f]);
  expect(evs.length).toBe(3);
  for (const e of evs) expect(e).toEqual([0xf8]);
});

test("ArduinoboyMaster: 0x7D → start (0xFA), 0x7E → stop (0xFC)", () => {
  const evs = decode([0x7d, 0x7f, 0x7f, 0x7e]);
  expect(evs.map((e) => e[0])).toEqual([0xfa, 0xf8, 0xf8, 0xfc]);
});

test("ArduinoboyMaster: 0x70..0x73 + value → NoteOn ch 0..3", () => {
  expect(decode([0x70, 60])).toEqual([[0x90, 60, 0x7f]]); // ch 0, middle C
  expect(decode([0x73, 72])).toEqual([[0x93, 72, 0x7f]]); // ch 3
});

test("ArduinoboyMaster: 0x7X note command with value 0 → NoteOff", () => {
  expect(decode([0x71, 0])).toEqual([[0x81, 0, 0]]); // NoteOff ch 1
});

test("ArduinoboyMaster: 0x74..0x77 + value → Control Change", () => {
  expect(decode([0x74, 64])).toEqual([[0xb0, 4, 64]]); // CC ch 0, CC# = m (simplified), value 64
});

test("ArduinoboyMaster: 0x78..0x7B + value → Program Change", () => {
  expect(decode([0x7a, 7])).toEqual([[0xc2, 7]]); // PC ch 2, patch 7
});

test("ArduinoboyMaster: realtime bytes interleave with command/value pairs", () => {
  const evs = decode([0x7d, 0x7f, 0x71, 60, 0x7f, 0x7e]);
  expect(evs).toEqual([[0xfa], [0xf8], [0x91, 60, 0x7f], [0xf8], [0xfc]]);
});

test("ArduinoboyMaster: value bytes without a pending command are dropped", () => {
  expect(decode([0x40, 0x7f])).toEqual([[0xf8]]); // 0x40 dropped, only the clock survives
});

test("ArduinoboyMaster: high-bit bytes (>=0x80) are ignored", () => {
  expect(decode([0xff, 0xaa, 0x55, 0xd5, 0x7f])).toEqual([[0xf8]]);
});

test("ArduinoboyMaster: reset clears pending state mid-command", () => {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeByte(0x71, state, (d) => out.push(d)); // pending NoteOn ch 1
  expect(out.length).toBe(0);
  arduinoboyReset(state);
  arduinoboyDecodeByte(60, state, (d) => out.push(d)); // would complete the note; now dropped
  expect(out.length).toBe(0);
  arduinoboyDecodeByte(0x7f, state, (d) => out.push(d)); // realtime still works
  expect(out).toEqual([[0xf8]]);
});

test("ArduinoboyMaster: command 0x7C consumes its value but emits nothing (m=0xC undefined)", () => {
  expect(decode([0x7c, 42, 0x7f])).toEqual([[0xf8]]); // only the clock survives
});

// --- arduinoboyDecodeSerialOut: the flag-gated framing --------------------------------------------

// Flag-frame a protocol byte the way LSDj's wire does: a 1 flag bit then the 7 payload bits (MSB-first),
// packed into a single captured 8-bit byte (so a data byte reads as 0x80 | payload).
const frame = (cmd: number): number => 0x80 | (cmd & 0x7f);

function decodeSerial(bytes: number[]): number[][] {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeSerialOut(bytes, state, (d) => out.push(d));
  return out;
}

test("framing: a stream of framed protocol bytes decodes to the same MIDI as the raw bytes", () => {
  // START, clock, NoteOn ch0 note 0x69, clock — each as one flag-framed captured byte.
  const evs = decodeSerial([frame(0x7d), frame(0x7f), frame(0x70), frame(0x69), frame(0x7f)]);
  expect(evs).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f], [0xf8]]);
});

test("framing: idle (flag=0) bits between frames are skipped", () => {
  // 0x00 = eight idle bits (flag 0 each); they carry no command and must not corrupt the stream.
  const evs = decodeSerial([0x00, frame(0x7f), 0x00, frame(0x7d)]);
  expect(evs).toEqual([[0xf8], [0xfa]]);
});

test("framing: a frame straddling a captured-byte boundary buffers, then decodes once", () => {
  // Bit stream (MSB-first): one idle bit (flag 0), then a framed clock (flag 1 + 7 payload = 0x7F) —
  // 9 bits total, so the frame's final payload bit lands in the SECOND captured byte. First byte
  // 0x7F = `0 1111111` (idle + flag + 6 payload bits); second byte 0x80 = `1 0000000` (last payload
  // bit + idle padding). The decoder must buffer the partial frame across the two calls and emit once.
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeSerialOut([0x7f], state, (d) => out.push(d));
  expect(out.length).toBe(0); // partial frame buffered, nothing decoded yet
  arduinoboyDecodeSerialOut([0x80], state, (d) => out.push(d));
  expect(out).toEqual([[0xf8]]); // the straddling clock frame completes exactly once
});

// --- integration: the mode-7 lsdj-sync role through the real DSP kernel --------------------------

const baseDyn = (): BlockInput => ({
  frames: 1024, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false,
  midiIn: [], buttons: [], keys: [], serialOut: [],
});

test("lsdj-sync mode 7: kernel fans serialOut to the role, which decodes it to emitMidiOut", () => {
  // The full TS pipeline: BlockInput.serialOut → kernel per-system fan → ctx.serialOut → case 7 →
  // arduinoboyDecodeSerialOut → emitMidiOut. Each captured byte flag-frames one protocol byte (0x80|cmd).
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  const k = new DspKernel(reg);
  k.setSystems({
    project: [{ kind: "midi-routing", config: { mode: 0 } }],
    systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: 7 } }] }],
  });
  const frame = (cmd: number) => ({ system: 1, byte: 0x80 | (cmd & 0x7f) });
  const out = k.processBlock({
    ...baseDyn(),
    serialOut: [frame(0x7d), frame(0x7f), frame(0x70), frame(0x69)], // START, clock, NoteOn ch0 note 0x69
  });
  expect(out.midiOut.map((m) => m.data)).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f]]);
  expect(out.serialIn.length).toBe(0); // mode 7 is a pure decoder — it never feeds LSDj serial-in
});

// --- Master Sync (mode 8, SYNC=LSDJ): each captured byte → one MIDI clock -------------------------

// Run per-block serial-out through a fresh master-sync decoder; each element is one block's bytes.
function masterSync(blocks: number[][]): number[][] {
  const out: number[][] = [];
  const state: MasterSyncState = {};
  for (const b of blocks) arduinoboyMasterSyncBlock(b, state, (d) => out.push(d));
  return out;
}

test("MasterSync: first byte → NoteOn(row) + start then a clock; the rest of the block → clocks", () => {
  expect(masterSync([[5, 5, 5]])).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8], [0xf8]]);
});

test("MasterSync: clocks continue across blocks without re-announcing the start", () => {
  // First byte announces the run; later blocks are clock-only (one 0xF8 per byte).
  expect(masterSync([[9], [9], [9]])).toEqual([[0x90, 9, 0x7f], [0xfa], [0xf8], [0xf8], [0xf8]]);
});

test("MasterSync: empty (between-tick) blocks emit nothing", () => {
  expect(masterSync([[], [], []])).toEqual([]);
});

// A flooded block (> the idle threshold) is LSDj's stopped-idle link handshake, not tempo clocking.
const flood = (): number[] => Array.from({ length: 40 }, (_, i) => i & 0x7f);

test("MasterSync: a flooded block after a run → exactly one transport stop (0xFC), no clocks", () => {
  const out = masterSync([[3], flood(), flood()]);
  expect(out.slice(0, 3)).toEqual([[0x90, 3, 0x7f], [0xfa], [0xf8]]); // start bookend + first clock
  expect(out.filter((m) => m[0] === 0xf8).length).toBe(1); // the flood emits NO clocks
  expect(out.filter((m) => m[0] === 0xfc).length).toBe(1); // one stop, not one per flooded block
  expect(out[out.length - 1]).toEqual([0xfc]);
});

test("MasterSync: a flood before any run emits nothing (idle at rest, never started)", () => {
  expect(masterSync([flood(), flood()])).toEqual([]);
});

test("MasterSync: after a flood stop, a new tempo byte restarts the run with a fresh NoteOn + start", () => {
  const out = masterSync([[3], flood(), [7]]);
  const fc = out.findIndex((m) => m[0] === 0xfc);
  expect(out.slice(fc + 1)).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
});

test("lsdj-sync mode 8: kernel fans serialOut to the master-sync role → clock + start MIDI", () => {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  const k = new DspKernel(reg);
  k.setSystems({
    project: [{ kind: "midi-routing", config: { mode: 0 } }],
    systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: 8 } }] }],
  });
  const out = k.processBlock({ ...baseDyn(), serialOut: [{ system: 1, byte: 5 }, { system: 1, byte: 6 }] });
  expect(out.midiOut.map((m) => m.data)).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8]]);
  expect(out.serialIn.length).toBe(0); // master sync is a pure decoder — no serial-in
});
