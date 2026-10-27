// Unit tests for the Arduinoboy MIDIOUT (MI.OUT) decoder (src/lsdjArduinoboy.ts) — the TS port of the
// native ArduinoboyMaster role. Two layers:
//   - arduinoboyDecodeByte: the byte protocol. These 11 cases are a direct port of the native
//     packages/native/test/ArduinoboyMasterTests.cpp (same bytes in, same MIDI out).
//   - arduinoboyDecodeSerialOut: the flag-gated framing in front of it — the raw captured bytes LSDj
//     actually emits (each frame = 1 flag bit + 7 payload bits, MSB-first), decoded to the same MIDI.
// Then master sync's decoder, arduinoboyMasterSyncBlock. The rp.lsdj.sync node that runs them off its Game Boy's
// serial out (modes 7 and 8) is test/script/nodes/lsdj-sync.test.ts's.
import { test, expect } from "../../testing/harness";
import {
  arduinoboyDecodeByte,
  arduinoboyDecodeSerialOut,
  arduinoboyMasterSyncBlock,
  arduinoboyReset,
  type ArduinoboyState,
  type MasterSyncState,
} from "../../src/lsdjArduinoboy";

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

// --- Master Sync (mode 8, SYNC=LSDJ): each byte a clock, a silence the stop ------------------------------------

const RATE = 44100;
/** MASTER_SYNC_STOP_MS (200 ms) at RATE. */
const STOP_FRAMES = 8820;
const START = [0xfa];
const STOP = [0xfc];
const CLOCK = [0xf8];
const row = (r: number): number[] => [0x90, r, 0x7f];
const clocksOf = (n: number): number[][] => Array.from({ length: n }, () => CLOCK);

/** A byte timeline: [absolute frame, byte], ascending. */
type Timeline = [number, number][];
/** A playing LSDj's clock bytes at tempo 128, 860 frames apart at 44.1 kHz: `n` of `byte`, the first at `at`. */
const clocks = (at: number, n: number, byte = 9): Timeline => Array.from({ length: n }, (_, i): [number, number] => [at + i * 860, byte]);

/** `total` frames in blocks of `size` (the last one short). */
const even = (size: number) => (total: number): number[] => {
  const out: number[] = [];
  for (let at = 0; at < total; at += size) out.push(Math.min(size, total - at));
  return out;
};
/** `total` frames in blocks cycling through `sizes` (the last one short). */
const cycle = (sizes: number[]) => (total: number): number[] => {
  const out: number[] = [];
  for (let at = 0, i = 0; at < total; i++) {
    out.push(Math.min(sizes[i % sizes.length], total - at));
    at += out[out.length - 1];
  }
  return out;
};
// The FL-style splits the parity matrix runs (test-native/parity/blocks-tiny.ts): ragged, and a 256-frame buffer
// split at automation points.
const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];

/** Decode `timeline` in the blocks `split` cuts `total` frames into, fed as the feedback wire feeds the decoder: a
 *  block's bytes reach it with the next block, at their frames in the block that produced them. Each message with the
 *  index of the block it went out in. */
function masterSync(timeline: Timeline, split: (total: number) => number[], total: number, rate = RATE): { block: number; data: number[] }[] {
  const out: { block: number; data: number[] }[] = [];
  const state: MasterSyncState = {};
  let start = 0;
  let next = 0;
  let bytes: number[] = [];
  let frames: number[] = [];
  split(total).forEach((n, block) => {
    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (data) => out.push({ block, data }));
    bytes = [];
    frames = [];
    for (; next < timeline.length && timeline[next][0] < start + n; next++) {
      frames.push(timeline[next][0] - start);
      bytes.push(timeline[next][1]);
    }
    start += n;
  });
  return out;
}
const sent = (out: { data: number[] }[]): number[][] => out.map((m) => m.data);

test("MasterSync: every byte is a clock, and the first opens the run with the song-row NoteOn and 0xFA", () => {
  expect(sent(masterSync(clocks(100, 3, 5), even(1024), 4096))).toEqual([row(5), START, CLOCK, CLOCK, CLOCK]);
});

test("MasterSync: a silence longer than 200 ms closes the run with one 0xFC, a shorter one does not", () => {
  // 8000 frames between the first two bytes, then 21900 before a third, then the end.
  const out = sent(masterSync([[100, 9], [8100, 9], [30000, 7]], even(1024), 50000));
  expect(out).toEqual([row(9), START, CLOCK, CLOCK, STOP, row(7), START, CLOCK, STOP]);
});

test("MasterSync: before any byte, and while stopped, nothing is sent", () => {
  expect(sent(masterSync([], even(1024), 20000))).toEqual([]);
  expect(sent(masterSync([[100, 9]], even(1024), 40000))).toEqual([row(9), START, CLOCK, STOP]);
});

test("MasterSync: the 0xFC goes out in the first block that starts more than 200 ms after the last byte", () => {
  const stopBlock = (out: { block: number; data: number[] }[]) => out.find((m) => m.data[0] === 0xfc)?.block;
  // A byte at frame 100: the stop is due after frame 100 + STOP_FRAMES = 8920.
  expect(stopBlock(masterSync([[100, 9]], even(1024), 20000))).toBe(Math.floor((100 + STOP_FRAMES) / 1024) + 1); // 9216
  expect(stopBlock(masterSync([[100, 9]], even(1), 10000))).toBe(100 + STOP_FRAMES + 1);
});

test("MasterSync: the silence is time, so 10000 frames is a stop at 44.1 kHz and not at 96 kHz", () => {
  const timeline: Timeline = [[100, 1], [10100, 1]];
  expect(sent(masterSync(timeline, even(1024), 12000, 44100))).toEqual([row(1), START, CLOCK, STOP, row(1), START, CLOCK]);
  expect(sent(masterSync(timeline, even(1024), 12000, 96000))).toEqual([row(1), START, CLOCK, CLOCK]);
});

test("MasterSync: one timeline decodes to the same messages however the host cuts its blocks", () => {
  // A run of 80 clocks, LSDj stopped for about a second, a run of 40, and LSDj stopped again.
  const timeline: Timeline = [...clocks(5000, 80), ...clocks(120000, 40, 3)];
  const expected = [row(9), START, ...clocksOf(80), STOP, row(3), START, ...clocksOf(40), STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(8192)])
    expect(sent(masterSync(timeline, split, 170000))).toEqual(expected);
});

test("MasterSync: a silence that ends inside one call still stops the run, however the host cuts its blocks", () => {
  // The silence before frame 18100 passes 200 ms at frame 16920, and no 4096- or 8192-frame block starts between
  // that and the byte that ends it, so the byte reaches the decoder in the same call as the silence's end.
  const timeline: Timeline = [[100, 1], [8100, 1], [18100, 2], [40000, 3]];
  const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP, row(3), START, CLOCK, STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
    expect(sent(masterSync(timeline, split, 60000))).toEqual(expected);
});

test("MasterSync: the checks before a byte and at a block start share the stop's boundary: exactly 200 ms is no stop, a frame more is", () => {
  // The second byte comes exactly STOP_FRAMES after the first, the third STOP_FRAMES + 1 after the second. In 1-frame
  // blocks a block starts at each mark, so the block-start check meets both; in longer ones the check before each
  // byte does.
  const timeline: Timeline = [[100, 1], [100 + STOP_FRAMES, 1], [100 + 2 * STOP_FRAMES + 1, 2]];
  const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
    expect(sent(masterSync(timeline, split, 40000))).toEqual(expected);
});
